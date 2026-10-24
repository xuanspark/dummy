// ============================================================================
//  main.cpp —— 35 步进子板主入口
// ----------------------------------------------------------------------------
//  - 主控：STM32F103CB @ 72 MHz，与 REF 主控以 CAN 总线联动
//  - 驱动：TB67H450 双 H 桥 + MT6816 磁编码器，闭环 DCE 控制
//  - 与 42motor 项目完全镜像，仅默认 dce_kp = 195（小型 35 mm 步进需更低增益）
//  - 节点 ID 由 PA8/9/10 三位拨码开关读出
//  - 100 Hz 定时器：按键扫描 + LED 状态 + 可选温度采样
//  - 20 kHz 定时器：控制环 / 编码器校准 (Tick20kHz)
//  - EEPROM 优先则：EEPROM > Motor.h 默认值
// ============================================================================
#include "common_inc.h"
#include "configurations.h"
#include "Platform/Utils/st_hardware.h"
#include <tim.h>


/* 组件全局定义 ----------------------------------------------------- */
BoardConfig_t boardConfig;                      // EEPROM 中的本板配置快照
Motor motor;                                    // 顶层电机对象（驱动 + 编码器 + 运动规划 + 控制器）
TB67H450 tb67H450;                              // 双 H 桥驱动芯片
MT6816 mt6816;                                  // 磁编码器
EncoderCalibrator encoderCalibrator(&motor);    // 编码器校准器（出厂 / 长按按键触发）
Button button1(1, 1000), button2(2, 3000);      // 两个按键（1 秒 / 3 秒 长按阈值）
void OnButton1Event(Button::Event _event);
void OnButton2Event(Button::Event _event);
Led statusLed;                                  // 状态指示 LED

/* 主入口 -------------------------------------------------------------- */
void Main()
{
    uint64_t serialNum = GetSerialNumber();
    uint16_t defaultNodeID = 0;
    // ID 拨码开关（3 位二进制 = 0~7 节点号）：
    //   ID0 = PA8  --> 拨码 4
    //   ID1 = PA9  --> 拨码 3
    //   ID2 = PA10 --> 拨码 2
    uint16_t nodeID = !HAL_GPIO_ReadPin(GPIOA, ID0_Pin);
    nodeID |= !HAL_GPIO_ReadPin(GPIOA, ID1_Pin) << 1;
    nodeID |= !HAL_GPIO_ReadPin(GPIOA, ID2_Pin) << 2;
    defaultNodeID = nodeID;

    /*---------- 从 EEPROM 读取配置 ----------*/
    // 优先顺序：EEPROM 内容 > Motor.h 默认值
    EEPROM eeprom;
    eeprom.get(0, boardConfig);
    if (boardConfig.configStatus != CONFIG_OK) // EEPROM 为空或损坏，写入默认配置
    {
        // 手爪固件默认值（与 35motor 关节版的差异）：
        //   defaultMode = MODE_STOP        上电先安全停机，避免误抓
        //   enableStallProtect = true      启用堵转保护，作为"夹住物体"的判别基础
        //   currentLimit = 1000 mA         夹紧电流上限（ref 端默认下发 700 mA）
        //   其他 DCE / 速度参数与 35motor 保持一致，避免改动过多影响稳定性
        boardConfig = BoardConfig_t{
            .configStatus = CONFIG_OK,
//            .canNodeId = defaultNodeID,
            .encoderHomeOffset = 0,
            .defaultMode = Motor::MODE_STOP,
            .currentLimit = 1 * 1000,    // mA
            .velocityLimit = 30 * motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS, // r/s
            .velocityAcc = 100 * motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS,   // r/s^2
            .calibrationCurrent=2000,
            .dce_kp = 195,
            .dce_kv = 80,
            .dce_ki = 300,
            .dce_kd = 250,
            .motor_temperature = 0.0,
            .enableMotorOnBoot=false,
            .enableStallProtect=true,
            .enableTempWatch=false,
        };
        eeprom.put(0, boardConfig);
    }
    boardConfig.enableTempWatch=false;
    // 手爪固件强制：堵转保护始终开启，覆盖老 EEPROM 里的 false
    // （否则未擦 EEPROM 烧 35hand 的板子会沿用 35motor 默认 enableStallProtect=false，
    // 导致 isStalled 永远不被检测，#GETJPOS 拿不到 H:2）
    boardConfig.enableStallProtect = true;
    // 节点 ID 不从 EEPROM 读，始终以 3 位拨码开关为准
    boardConfig.canNodeId = defaultNodeID;
    motor.config.motionParams.encoderHomeOffset = boardConfig.encoderHomeOffset;
    motor.config.motionParams.ratedCurrent = boardConfig.currentLimit;
    motor.config.motionParams.ratedVelocity = boardConfig.velocityLimit;
    motor.config.motionParams.ratedVelocityAcc = boardConfig.velocityAcc;
    motor.motionPlanner.velocityTracker.SetVelocityAcc(boardConfig.velocityAcc);
    motor.motionPlanner.positionTracker.SetVelocityAcc(boardConfig.velocityAcc);
    motor.config.motionParams.caliCurrent = boardConfig.calibrationCurrent;
    motor.config.ctrlParams.dce.kp = boardConfig.dce_kp;
    motor.config.ctrlParams.dce.kv = boardConfig.dce_kv;
    motor.config.ctrlParams.dce.ki = boardConfig.dce_ki;
    motor.config.ctrlParams.dce.kd = boardConfig.dce_kd;
    motor.config.ctrlParams.stallProtectSwitch = boardConfig.enableStallProtect;


//    printf("encoderHomeOffset[%d] ratedCurrent[%d] ratedVelocity[%d] ratedVelocityAcc[%d]\r\n" , boardConfig.encoderHomeOffset, boardConfig.currentLimit, boardConfig.velocityLimit, boardConfig.velocityAcc);
//    printf("velocityTracker[%d] positionTracker[%d] caliCurrent[%d] \r\n" , boardConfig.velocityAcc, boardConfig.velocityAcc);
//    printf("kp[%d] kv[%d] ki[%d] kd[%d]\r\n" , boardConfig.dce_kp, boardConfig.dce_kv, boardConfig.dce_ki, boardConfig.dce_kd);


    /*---------------- 初始化电机三套件 ----------------*/
    motor.AttachDriver(&tb67H450);    // 绑定驱动芯片
    motor.AttachEncoder(&mt6816);     // 绑定编码器
    motor.controller->Init();
    motor.driver->Init();
    motor.encoder->Init();


    /*------------- 初始化外设 -------------*/
    button1.SetOnEventListener(OnButton1Event);
    button2.SetOnEventListener(OnButton2Event);


    /*------- 启动闭环控制定时器 ------*/
    HAL_Delay(100);
    HAL_TIM_Base_Start_IT(&htim1);  // 100 Hz周期，处理按键 / LED / 温度采样
    HAL_TIM_Base_Start_IT(&htim4);  // 20 kHz周期，处理控制环 / 校准

    // 同时按住两键开机 → 进入编码器校准流程
    if (button1.IsPressed() && button2.IsPressed())
        encoderCalibrator.isTriggered = true;
//    printf("start motor %d\r\n", defaultNodeID);

    for (;;)
    {
        // 主循环：编码器校准轮询 + EEPROM 提交 / 恢复检查
//        if ( encoderCalibrator.FlashRun() == 0)
            encoderCalibrator.TickMainLoop();

        if (boardConfig.configStatus == CONFIG_COMMIT)
        {
            // 提交：将编辑后的配置制入 EEPROM
            boardConfig.configStatus = CONFIG_OK;
            eeprom.put(0, boardConfig);
        } else if (boardConfig.configStatus == CONFIG_RESTORE)
        {
            // 恢复：写回 EEPROM 后立即重启令配置生效
            eeprom.put(0, boardConfig);
            HAL_NVIC_SystemReset();
        }
    }
}


/* 事件回调 ----------------------------------------------------------- */
// 全局计数供温度采样使用（1 s = 100 个 100 Hz tick）
uint32_t count;
// 100 Hz 定时器回调。处理按键 + LED + 可选温度采样。
extern "C" void Tim1Callback100Hz()
{
    __HAL_TIM_CLEAR_IT(&htim1, TIM_IT_UPDATE);

    button1.Tick(10);
    button2.Tick(10);
    statusLed.Tick(10, motor.controller->state);
    // 每 1 秒（100 个 tick）采样一次温度，中间点 LED 闪以示警
    if (boardConfig.enableTempWatch)
    {
        count ++;
        if ( count >= 100)
        {
            boardConfig.motor_temperature = AdcGetChipTemperature();
            count = 0;
        }
        if (count > 50)
        {
            statusLed.Status(1, true);
        }
        else
        {
            statusLed.Status(1, false);
        }
    }
}


// 20 kHz 定时器回调。控制环主体：正常跑 motor.Tick20kHz，校准期间跑校准逻辑。
extern "C" void Tim4Callback20kHz()
{
    __HAL_TIM_CLEAR_IT(&htim4, TIM_IT_UPDATE);

    if (encoderCalibrator.isTriggered)
        encoderCalibrator.Tick20kHz();
    else
        motor.Tick20kHz();
}


// 按键 1：长按进入编码器校准；短按在 STOP 与上一位运行模式之间切换
void OnButton1Event(Button::Event _event)
{
    switch (_event)
    {
        case ButtonBase::UP:
            break;
        case ButtonBase::DOWN:
            break;
        case ButtonBase::LONG_PRESS:
            // 长按：进入编码器校准路径（Tick20kHz 会被主控环改向到 calibrator）
            encoderCalibrator.isTriggered = true;
            encoderCalibrator.Tick20kHz();
            encoderCalibrator.TickMainLoop();
//            HAL_NVIC_SystemReset();
//            encoderCalibrator.TestFlash();
            break;
        case ButtonBase::CLICK:
            // 短按：在 STOP 与上次运行模式之间切换
            printf("KEY1\r\n");
            if (motor.controller->modeRunning != Motor::MODE_STOP)
            {
                boardConfig.defaultMode = motor.controller->modeRunning;
                motor.controller->requestMode = Motor::MODE_STOP;
            } else
            {
                motor.controller->requestMode = static_cast<Motor::Mode_t>(boardConfig.defaultMode);
            }
            break;
    }
}


// 按键 2：长按将当前运行模式下的设点归零；短按清除堆转报警。
void OnButton2Event(Button::Event _event)
{
    switch (_event)
    {
        case ButtonBase::UP:
            break;
        case ButtonBase::DOWN:
            break;
        case ButtonBase::LONG_PRESS:
            switch (motor.controller->modeRunning)
            {
                case Motor::MODE_COMMAND_CURRENT:
                case Motor::MODE_PWM_CURRENT:
                    motor.controller->SetCurrentSetPoint(0);
                    break;
                case Motor::MODE_COMMAND_VELOCITY:
                case Motor::MODE_PWM_VELOCITY:
                    motor.controller->SetVelocitySetPoint(0);
                    break;
                case Motor::MODE_COMMAND_POSITION:
                case Motor::MODE_PWM_POSITION:
                    motor.controller->SetPositionSetPoint(0);
                    break;
                case Motor::MODE_COMMAND_Trajectory:
                case Motor::MODE_STEP_DIR:
                case Motor::MODE_STOP:
                    break;
            }
            break;
        case ButtonBase::CLICK:
            printf("KEY2\r\n");
            motor.controller->ClearStallFlag();
            break;
    }
}
