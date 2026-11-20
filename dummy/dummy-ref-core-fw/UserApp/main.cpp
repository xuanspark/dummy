// ============================================================================
//  main.cpp —— ref 主控用户应用入口
// ----------------------------------------------------------------------------
//  - 初始化通信、机器人对象、RGB、OLED/IMU 等用户层模块
//  - 创建实时控制、通信处理、RGB 刷新与在线检测等 FreeRTOS 任务
//  - 定时器中断只负责唤醒任务，耗时逻辑均放在线程上下文执行
// ============================================================================

#include "common_inc.h"


// On-board Screen, can choose from hi2c2 or hi2c0(soft i2c)
SSD1306 oled(&hi2c0);
// On-board Sensor, used hi2c1
MPU6050 mpu6050(&hi2c1);
// 5 User-Timers, can choose from htim7/htim10/htim11/htim13/htim14
Timer timerCtrlLoop(&htim7, 200);
// 2x2-channel PWMs, used htim9 & htim12, each has 2-channel outputs
PWM pwm(21000, 21000);

RGB rgb(0);
// Robot instance
DummyRobot dummy(&hcan1);

static volatile bool imuReady = false;


/* Thread Definitions -----------------------------------------------------*/
osThreadId_t controlLoopFixUpdateHandle;
static void ReportRuntimeHealth();

void ThreadControlLoopFixUpdate(void* argument)
{
    for (;;)
    {
        // Suspended here until got Notification.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (dummy.IsEnabled() && dummy.robotState != DummyRobot::STATE_EMERGENCY_STOP)
        {
            // NORMAL 与 PAUSED 都正常运行控制环：
            //   - NORMAL：常规分发命令；
            //   - PAUSED：让当前未完成的动作自然继续到目标点；新命令在 ParseCommand 入口阻塞。
            dummy.PollJointAngle();
            if (dummy.IsMoving())
                dummy.UpdateJointPose6D();
        } else if (dummy.IsEnabled() && dummy.robotState == DummyRobot::STATE_EMERGENCY_STOP)
        {
            // 急停：每 tick 主动广播 V=0 实现机械制动（幂等）。
            // 这保证即使状态由 USB ISR 偷拍设置，也能在 5ms 内真正刹车，
            // 不依赖 OnUsbAsciiCmd 调用 EmergencyStop()。
            dummy.ApplyEmergencyBrake();
            dummy.PollJointAngle();
            // 同步 targetJoints 为当前位置，避免 RESUME 后 MoveJoints 跳动
            dummy.UpdateJointPose6D();
        } else
        {
            // 未使能：只刷新关节状态
            dummy.PollJointAngle();
            dummy.UpdateJointPose6D();
        }
    }
}


osThreadId_t ControlLoopUpdateHandle;
void ThreadControlLoopUpdate(void* argument)
{
    char command[128];
    for (;;)
    {
        if (dummy.commandHandler.Pop(command, sizeof(command), osWaitForever))
            dummy.commandHandler.ParseCommand(command);
    }
}


osThreadId_t oledTaskHandle;
void ThreadOledUpdate(void* argument)
{
    uint32_t t = micros();
    uint32_t healthReportElapsedMs = 0;
    char buf[16];
    char cmdModeNames[4][4] = {"SEQ", "INT", "TRJ", "TUN"};

    for (;;)
    {
        if (imuReady)
            mpu6050.Update(true);

        oled.clearBuffer();
        oled.setFont(u8g2_font_5x8_tr);
        oled.setCursor(0, 10);
        oled.printf(imuReady ? "IMU:%.3f/%.3f" : "IMU:OFF", mpu6050.data.ax, mpu6050.data.ay);
        oled.setCursor(85, 10);
        const uint32_t now = micros();
        const uint32_t elapsed = now - t;
        const uint32_t fps = (elapsed == 0U) ? 0U : (1000000U / elapsed);
        oled.printf("| FPS:%lu", (unsigned long) fps);
        t = now;

        oled.drawBox(0, 15, 128, 3);
        oled.setCursor(0, 30);
        oled.printf(">%3d|%3d|%3d|%3d|%3d|%3d",
                    (int) roundf(dummy.currentJoints.a[0]), (int) roundf(dummy.currentJoints.a[1]),
                    (int) roundf(dummy.currentJoints.a[2]), (int) roundf(dummy.currentJoints.a[3]),
                    (int) roundf(dummy.currentJoints.a[4]), (int) roundf(dummy.currentJoints.a[5]));

        oled.drawBox(40, 35, 128, 24);
        oled.setFont(u8g2_font_6x12_tr);
        oled.setDrawColor(0);
        oled.setCursor(42, 45);
        oled.printf("%4d|%4d|%4d", (int) roundf(dummy.currentPose6D.X),
                    (int) roundf(dummy.currentPose6D.Y), (int) roundf(dummy.currentPose6D.Z));
        oled.setCursor(42, 56);
        oled.printf("%4d|%4d|%4d", (int) roundf(dummy.currentPose6D.A),
                    (int) roundf(dummy.currentPose6D.B), (int) roundf(dummy.currentPose6D.C));
        oled.setDrawColor(1);
        oled.setCursor(0, 45);
        oled.printf("[XYZ]:");
        oled.setCursor(0, 56);
        oled.printf("[ABC]:");

        oled.setFont(u8g2_font_10x20_tr);
        oled.setCursor(0, 78);
        if (dummy.IsEnabled())
        {
            for (int i = 1; i <= 6; i++)
                buf[i - 1] = (dummy.jointsStateFlag & (1 << i) ? '*' : '_');
            buf[6] = 0;
            oled.printf("[%s] %s", cmdModeNames[dummy.commandMode - 1], buf);
        } else
        {
            oled.printf("[%s] %s", cmdModeNames[dummy.commandMode - 1], "======");
        }

        oled.sendBuffer();
        healthReportElapsedMs += 50;
        if (healthReportElapsedMs >= 5000)
        {
            healthReportElapsedMs = 0;
            ReportRuntimeHealth();
        }
        osDelay(50);
    }
}


/* Timer Callbacks -------------------------------------------------------*/
void OnTimer7Callback()
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Wake & invoke thread IMMEDIATELY.
    vTaskNotifyGiveFromISR(TaskHandle_t(controlLoopFixUpdateHandle), &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

extern "C" void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM5)
    {
        rgb.Interrupt(1);
    }
}

osThreadId_t rgbTaskHandle;
static UBaseType_t StackWatermark(osThreadId_t handle)
{
    return handle ? uxTaskGetStackHighWaterMark((TaskHandle_t) handle) : 0;
}

static void ReportRuntimeHealth()
{
    if (!uart4StreamOutputPtr) return;
    Respond(*uart4StreamOutputPtr,
            "[sys] heap=%lu min_heap=%lu stack_words{comm=%lu usb=%lu uart=%lu "
            "ctrl=%lu cmd=%lu oled=%lu rgb=%lu usb_rx_drop=%lu}\n",
            (unsigned long) xPortGetFreeHeapSize(),
            (unsigned long) xPortGetMinimumEverFreeHeapSize(),
            (unsigned long) StackWatermark(commTaskHandle),
            (unsigned long) StackWatermark(usbServerTaskHandle),
            (unsigned long) StackWatermark(uartServerTaskHandle),
            (unsigned long) StackWatermark(controlLoopFixUpdateHandle),
            (unsigned long) StackWatermark(ControlLoopUpdateHandle),
            (unsigned long) StackWatermark(oledTaskHandle),
            (unsigned long) StackWatermark(rgbTaskHandle),
            (unsigned long) usb_stats_.rx_drop_cnt);
}

void ThreadRGBUpdate(void* argument) {
    for (;;) {
        if (dummy.GetRGBEnabled())
        {
            rgb.Run((RGB::Rgb_style_t)dummy.GetRGBMode());
        }else
        {
            rgb.Run(RGB::MODE_OFF);
        }
        // 自动模式 5 秒空闲回待机计时（每 tick 约30ms）
        dummy.AutoModeIdleTick(30);
        // 在线检测定时 tick（5 分钟，工作时不检测）
        dummy.OnlineCheckTick(30);
        osDelay(30);
    }
}

/* Default Entry -------------------------------------------------------*/
void Main(void)
{
    // Init all communication staff, including USB-CDC/VCP/UART/CAN etc.
    InitCommunication();

    // Init Robot.
    dummy.Init();
    // 从 EEPROM 加载上次保存的 RGB 模式
    dummy.LoadRGBModeFromEEPROM();
    // 初始化异步系统命令线程（HOME/START/RESET/CLOSE 非阻塞执行）
    dummy.InitSysCmdThread();
    InitAsciiCommands();

    // Init IMU with a bounded retry count so a missing sensor cannot block OLED startup.
    for (uint32_t attempt = 0; attempt < 5; ++attempt)
    {
        mpu6050.Init();
        if (mpu6050.testConnection())
        {
            imuReady = true;
            break;
        }
        osDelay(100);
    }
    if (imuReady)
        mpu6050.InitFilter(200, 100, 50);

    // Init OLED 128x80.
    oled.Init();
    rgb.InitHardware();
    // PA3 专用于 WS2812 数据，不能启动 TIM9_CH2；只启动实际使用的 CH_A1。
    pwm.Start(PWM::CH_A1);

    // Init & Run User Threads.
    const osThreadAttr_t controlLoopTask_attributes = {
        .name = "ControlLoopFixUpdateTask",
        .stack_size = 2000,
        .priority = (osPriority_t) osPriorityRealtime,
    };
    controlLoopFixUpdateHandle = osThreadNew(ThreadControlLoopFixUpdate, nullptr,
                                             &controlLoopTask_attributes);
    if (!controlLoopFixUpdateHandle) HandleRuntimeInitFailure();

    const osThreadAttr_t ControlLoopUpdateTask_attributes = {
        .name = "ControlLoopUpdateTask",
        .stack_size = 2000,
        .priority = (osPriority_t) osPriorityNormal,
    };
    ControlLoopUpdateHandle = osThreadNew(ThreadControlLoopUpdate, nullptr,
                                          &ControlLoopUpdateTask_attributes);
    if (!ControlLoopUpdateHandle) HandleRuntimeInitFailure();

    const osThreadAttr_t oledTask_attributes = {
        .name = "OledTask",
        .stack_size = 2000,
        .priority = (osPriority_t) osPriorityNormal,   // should >= Normal
    };
    oledTaskHandle = osThreadNew(ThreadOledUpdate, nullptr, &oledTask_attributes);
    if (!oledTaskHandle) HandleRuntimeInitFailure();

    const osThreadAttr_t rgbTask_attributes = {
            .name = "RGBTask",
            .stack_size = 2000,
            .priority = (osPriority_t) osPriorityNormal,   // should >= Normal
    };
    rgbTaskHandle = osThreadNew(ThreadRGBUpdate, nullptr, &rgbTask_attributes);
    if (!rgbTaskHandle) HandleRuntimeInitFailure();

    // Start Timer Callbacks.
    timerCtrlLoop.SetCallback(OnTimer7Callback);
    timerCtrlLoop.Start();

    // System started, light switch-led up.
    if (!imuReady)
        Respond(*uart4StreamOutputPtr, "[sys] IMU unavailable; OLED continues without IMU data\n");
    ReportRuntimeHealth();
    pwm.SetDuty(PWM::CH_A1, 0.5);
}

