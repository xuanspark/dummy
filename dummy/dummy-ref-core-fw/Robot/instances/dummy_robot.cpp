// ============================================================================
//  dummy_robot.cpp —— Dummy 六轴机器人主状态机实现
// ----------------------------------------------------------------------------
//  - 管理 6 个关节电机、手爪、RGB 灯效、在线检测与运动命令队列
//  - 实现 START/HOME/RESET/CLOSE、急停/暂停/恢复等系统级动作
//  - 阻塞运动通过 SysCmdThread 异步执行，避免串口处理线程被长时间占用
// ============================================================================

#include "communication.hpp"
#include "dummy_robot.h"
#include "eeprom_interface.h"

extern RGB rgb;
static EEPROMClass eeprom;

inline float AbsMaxOf6(DOF6Kinematic::Joint6D_t _joints, uint8_t &_index)
{
    float max = -1;
    for (uint8_t i = 0; i < 6; i++)
    {
        if (abs(_joints.a[i]) > max)
        {
            max = abs(_joints.a[i]);
            _index = i;
        }
    }

    return max;
}


DummyRobot::DummyRobot(CAN_HandleTypeDef* _hcan) :
    hcan(_hcan)
{
    motorJ[ALL] = new CtrlStepMotor(_hcan, 0, false, 1, -180, 180);
    motorJ[1] = new CtrlStepMotor(_hcan, 1, false, 50, -170, 170);
    motorJ[2] = new CtrlStepMotor(_hcan, 2, true, 50, -75, 90);
    motorJ[3] = new CtrlStepMotor(_hcan, 3, true, 50, 35, 180);
    motorJ[4] = new CtrlStepMotor(_hcan, 4, true, 50, -180, 180);
    motorJ[5] = new CtrlStepMotor(_hcan, 5, true, 50, -100, 100);
    motorJ[6] = new CtrlStepMotor(_hcan, 6, true, 30, -180, 180);
    hand2 = new StepHand(_hcan, 7);

    dof6Solver = new DOF6Kinematic(0.134f, 0.035f, 0.146f, 0.117f, 0.052f, 0.0625f);
//    dof6Solver = new DOF6Kinematic(0.109f, 0.035f, 0.146f, 0.115f, 0.052f, 0.072f);
}


DummyRobot::~DummyRobot()
{
    for (int j = 0; j <= 6; j++)
        delete motorJ[j];

    delete hand2;
    delete dof6Solver;
}


void DummyRobot::Init()
{
    SetCommandMode(DEFAULT_COMMAND_MODE);
    SetJointSpeed(DEFAULT_JOINT_SPEED);
}


void DummyRobot::Reboot()
{
    motorJ[ALL]->Reboot();
    osDelay(500); // waiting for all joints done
    HAL_NVIC_SystemReset();
}

void DummyRobot::MoveJoints(DOF6Kinematic::Joint6D_t _joints)
{
    for (int j = 1; j <= 6; j++)
    {
        motorJ[j]->SetAngleWithVelocityLimit(_joints.a[j - 1] - initPose.a[j - 1],
                                             dynamicJointSpeeds.a[j - 1]);
    }
}


bool DummyRobot::MoveJ(float _j1, float _j2, float _j3, float _j4, float _j5, float _j6)
{
    DOF6Kinematic::Joint6D_t targetJointsTmp(_j1, _j2, _j3, _j4, _j5, _j6);
    bool valid = true;

    for (int j = 1; j <= 6; j++)
    {
        if (targetJointsTmp.a[j - 1] > motorJ[j]->angleLimitMax ||
            targetJointsTmp.a[j - 1] < motorJ[j]->angleLimitMin)
            valid = false;
    }

    if (valid)
    {
        DOF6Kinematic::Joint6D_t deltaJoints = targetJointsTmp - currentJoints;
        uint8_t index;
        float maxAngle = AbsMaxOf6(deltaJoints, index);
        float time = maxAngle * (float) (motorJ[index + 1]->reduction) / jointSpeed;
        for (int j = 1; j <= 6; j++)
        {
            dynamicJointSpeeds.a[j - 1] =
                abs(deltaJoints.a[j - 1] * (float) (motorJ[j]->reduction) / time * 0.1f); //0~10r/s
        }

        jointsStateFlag = 0;
        targetJoints = targetJointsTmp;

        // ★ 通用 Resume 机制：每次运动自动预置 pending 目标。
        // 目的：ESTOP 强制 jointsStateFlag=FINISH 会让阻塞 wait 循环瞬间退出，
        // 若在循环体里记 pending 就会被漏掉。这里在启动瞬间就记录，
        // 正常完成时由调用方或 NotifyWorkDone 清除；ESTOP 则保留供 Resume 续跑。
        pendingResumeTarget = targetJointsTmp;
        pendingResumeSpeed = jointSpeed;
        stoppedJointSpeed = jointSpeed;   // 默认：队列速度 = 当前速度
                                          // Homing/Resting 会在 MoveJ 后覆盖为用户原队列速度
        hasPendingResumeTarget = true;

        return true;
    }

    return false;
}


bool DummyRobot::MoveL(float _x, float _y, float _z, float _a, float _b, float _c)
{
    DOF6Kinematic::Pose6D_t pose6D(_x, _y, _z, _a, _b, _c);
    DOF6Kinematic::IKSolves_t ikSolves{};
    DOF6Kinematic::Joint6D_t lastJoint6D{};

    dof6Solver->SolveIK(pose6D, lastJoint6D, ikSolves);

    bool valid[8];
    int validCnt = 0;

    for (int i = 0; i < 8; i++)
    {
        valid[i] = true;

        for (int j = 1; j <= 6; j++)
        {
            if (ikSolves.config[i].a[j - 1] > motorJ[j]->angleLimitMax ||
                ikSolves.config[i].a[j - 1] < motorJ[j]->angleLimitMin)
            {
                valid[i] = false;
                continue;
            }
        }

        if (valid[i]) validCnt++;
    }

    if (validCnt)
    {
        float min = 1000;
        uint8_t indexConfig = 0, indexJoint = 0;
        for (int i = 0; i < 8; i++)
        {
            if (valid[i])
            {
                for (int j = 0; j < 6; j++)
                    lastJoint6D.a[j] = ikSolves.config[i].a[j];
                DOF6Kinematic::Joint6D_t tmp = currentJoints - lastJoint6D;
                float maxAngle = AbsMaxOf6(tmp, indexJoint);
                if (maxAngle < min)
                {
                    min = maxAngle;
                    indexConfig = i;
                }
            }
        }

        return MoveJ(ikSolves.config[indexConfig].a[0], ikSolves.config[indexConfig].a[1],
                     ikSolves.config[indexConfig].a[2], ikSolves.config[indexConfig].a[3],
                     ikSolves.config[indexConfig].a[4], ikSolves.config[indexConfig].a[5]);
    }

    return false;
}

void DummyRobot::UpdateJointAngles()
{
    motorJ[ALL]->UpdateAngle();
}


void DummyRobot::UpdateJointAnglesCallback()
{
    for (int i = 1; i <= 6; i++)
    {
        currentJoints.a[i - 1] = motorJ[i]->angle + initPose.a[i - 1];

        if (motorJ[i]->state == CtrlStepMotor::FINISH)
            jointsStateFlag |= (1 << i);
        else
            jointsStateFlag &= ~(1 << i);
    }


}


void DummyRobot::SetJointSpeed(float _speed)
{
    if (_speed < 0)_speed = 0;
    else if (_speed > 100) _speed = 100;

    jointSpeed = _speed * jointSpeedRatio;
}


void DummyRobot::SetJointAcceleration(float _acc)
{
    if (_acc < 0)_acc = 0;
    else if (_acc > 100) _acc = 100;

    for (int i = 1; i <= 6; i++)
        motorJ[i]->SetAcceleration(_acc / 100 * DEFAULT_JOINT_ACCELERATION_BASES.a[i - 1]);
}


void DummyRobot::CalibrateHomeOffset()
{
    // Disable FixUpdate, but not disable motors
    isEnabled = false;
    motorJ[ALL]->SetEnable(true);

    // 1.Manually move joints to L-Pose [precisely]
    // ...
    motorJ[2]->SetCurrentLimit(0.5);
    motorJ[3]->SetCurrentLimit(0.5);
    osDelay(500);

    // 2.Apply Home-Offset the first time
    motorJ[ALL]->ApplyPositionAsHome();
    osDelay(500);

    // 3.Go to Resting-Pose
    initPose = DOF6Kinematic::Joint6D_t(0, 0, 90, 0, 0, 0);
    currentJoints = DOF6Kinematic::Joint6D_t(0, 0, 90, 0, 0, 0);
    Resting();
    osDelay(500);

    // 4.Apply Home-Offset the second time
    motorJ[ALL]->ApplyPositionAsHome();
    osDelay(500);
    motorJ[2]->SetCurrentLimit(1);
    motorJ[3]->SetCurrentLimit(1);
    osDelay(500);

    Reboot();
}


void DummyRobot::Homing()
{
    float lastSpeed = jointSpeed;
    SetJointSpeed(10);

    MoveJ(0, 0, 90, 0, 0, 0);        // ★ MoveJ 内部自动预置 pending
    stoppedJointSpeed = lastSpeed;   // 覆盖：Resume 要恢复用户队列速度而非 Homing 低速
    MoveJoints(targetJoints);

    while (IsMoving())
    {
        if (robotState == STATE_EMERGENCY_STOP)
            break;   // pending 保留，Resume 会续跑
        osDelay(10);
    }

    // 正常完成：清除 pending
    if (robotState != STATE_EMERGENCY_STOP)
        hasPendingResumeTarget = false;

    SetJointSpeed(lastSpeed);
    isHomed = true;
}


void DummyRobot::Resting()
{
    float lastSpeed = jointSpeed;
    SetJointSpeed(10);

    MoveJ(REST_POSE.a[0], REST_POSE.a[1], REST_POSE.a[2],
          REST_POSE.a[3], REST_POSE.a[4], REST_POSE.a[5]);
    stoppedJointSpeed = lastSpeed;   // 覆盖 MoveJ 默认值
    MoveJoints(targetJoints);

    while (IsMoving())
    {
        if (robotState == STATE_EMERGENCY_STOP)
            break;
        osDelay(10);
    }

    if (robotState != STATE_EMERGENCY_STOP)
        hasPendingResumeTarget = false;

    SetJointSpeed(lastSpeed);
}


void DummyRobot::SetEnable(bool _enable)
{
    motorJ[ALL]->SetEnable(_enable);
    isEnabled = _enable;
    // 失能后位置可能漂移，需重新 home 才能重新接收运动命令
    if (!_enable)
        isHomed = false;
}


void DummyRobot::Startup()
{
    // !START：使能机械臂 + 使能手爪 + 自动闭合手爪
    SetEnable(true);
    if (hand2 != nullptr)
    {
        hand2->SetEnable(true);
        // 启动时自动闭合手爪（电流模式，与下方关节运动并行执行）
        hand2->SetAngleWithCurrentLimit(1);
    }

    // 读取当前实际关节角度
    UpdateJointAngles();

    // 检测是否已在零点姿态（REST_POSE），容差 ±0.1°
    bool atRest = true;
    for (int j = 0; j < 6; j++)
    {
        if (fabsf(currentJoints.a[j] - REST_POSE.a[j]) > 0.1f)
        {
            atRest = false;
            break;
        }
    }

    if (atRest)
    {
        // 已在零点位置 → 直接进入待机，无需运动
        SetRGBAutoSubState(RGB::AUTO_STANDBY);
    }
    else
    {
        // 不在零点 → 进入工作状态，回零，完成后启动 5 秒回待机计时
        NotifyWorkStart();
        Resting();
        NotifyWorkDone();   // → AUTO_COMPLETE → 5s → AUTO_STANDBY
    }

    // 零点姿态（REST_POSE）不是工作姿态（home），运动前仍需 !HOME
    isHomed = false;
}


void DummyRobot::Shutdown()
{
    // !CLOSE 与 !START 对称：检测是否已在零点姿态（REST_POSE，容差 ±0.1°）
    bool atRest = true;
    if (isEnabled)
    {
        UpdateJointAngles();
        for (int j = 0; j < 6; j++)
        {
            if (fabsf(currentJoints.a[j] - REST_POSE.a[j]) > 0.1f)
            {
                atRest = false;
                break;
            }
        }
    }

    // 关闭前先发起手爪闭合动作（电流模式，与下方关节运动并行）
    if (hand2 != nullptr && hand2->isEnabled())
        hand2->SetAngleWithCurrentLimit(1);

    if (atRest)
    {
        // 已在零点姿态（或本来就失能）→ 短暂等待手爪闭合到位再失能
        if (hand2 != nullptr && hand2->isEnabled())
            osDelay(800);   // 等手爪夹紧到位
        if (hand2 != nullptr)
            hand2->SetEnable(false);
        SetEnable(false);  // 内部会清 isHomed
        SetRGBAutoSubState(RGB::AUTO_STANDBY);
    }
    else
    {
        // 不在零点 → 工作(绿) → 回零（期间手爪并行合上）→ 失能 → 完成(青，5s 后自动 → 白)
        NotifyWorkStart();
        Resting();
        if (hand2 != nullptr)
            hand2->SetEnable(false);
        SetEnable(false);  // 内部会清 isHomed
        NotifyWorkDone();   // → AUTO_COMPLETE → 5s → AUTO_STANDBY
    }
}


// ==================== 异步系统命令线程 ====================
// 阻塞命令（HOME/START/RESET/CLOSE）在独立线程执行，
// UART 处理线程保持畅通以响应 STOP/PAUSE 等即时命令。

static osMessageQueueId_t s_sysCmdQueue = nullptr;

static StreamSink* GetStreamByChannel(uint8_t channelType)
{
    switch (channelType) {
        case StreamSink::CHANNEL_TYPE_USB:   return usbStreamOutputPtr;
        case StreamSink::CHANNEL_TYPE_UART4: return uart4StreamOutputPtr;
        default: return uart4StreamOutputPtr;
    }
}

static void SysCmdThreadEntry(void* arg)
{
    DummyRobot* robot = (DummyRobot*)arg;
    DummyRobot::SysCmdMsg msg;

    for (;;)
    {
        if (osMessageQueueGet(s_sysCmdQueue, &msg, nullptr, osWaitForever) != osOK)
            continue;

        StreamSink* ch = GetStreamByChannel(msg.channelType);

        switch (msg.cmd)
        {
            case DummyRobot::SYSCMD_STARTUP:
                robot->Startup();
                Respond(*ch, "Start OK");
                break;
            case DummyRobot::SYSCMD_HOME:
                robot->NotifyWorkStart();
                robot->Homing();
                robot->NotifyWorkDone();
                Respond(*ch, "Home OK");
                break;
            case DummyRobot::SYSCMD_RESET:
                robot->NotifyWorkStart();
                robot->Resting();
                robot->NotifyWorkDone();
                Respond(*ch, "Reset OK");
                break;
            case DummyRobot::SYSCMD_SHUTDOWN:
                robot->Shutdown();
                Respond(*ch, "Close OK");
                break;
            default:
                break;
        }
    }
}

void DummyRobot::InitSysCmdThread()
{
    s_sysCmdQueue = osMessageQueueNew(4, sizeof(SysCmdMsg), nullptr);
    const osThreadAttr_t attr = {
        .name = "SysCmdThread",
        .stack_size = 2000,
        .priority = (osPriority_t) osPriorityNormal,
    };
    osThreadNew(SysCmdThreadEntry, this, &attr);
}

bool DummyRobot::PostSysCmd(SysCmd cmd, uint8_t channelType)
{
    if (s_sysCmdQueue == nullptr) return false;
    SysCmdMsg msg = {cmd, channelType};
    return osMessageQueuePut(s_sysCmdQueue, &msg, 0, 0) == osOK;
}


void DummyRobot::OnHandActionRGBHook(bool _open)
{
    // 手爪开/合都走完整流程：工作(绿) → 立即完成(青) → 5s 后自动回到待机(白)
    // _open 参数保留以兼容现有接口，张开和闭合的灯光行为一致
    (void)_open;
    NotifyWorkStart();
    NotifyWorkDone();
}

void DummyRobot::SetRGBEnable(bool _enable)
{
    isRGBEnabled = _enable;
}

bool DummyRobot::GetRGBEnabled()
{
    return isRGBEnabled;
}

void DummyRobot::SetRGBMode(uint32_t mode)
{
    rgbMode = mode;
    SaveRGBModeToEEPROM();
}

uint32_t DummyRobot::GetRGBMode()
{
    return rgbMode;
}

void DummyRobot::LoadRGBModeFromEEPROM()
{
    uint8_t val = eeprom.read(EEPROM_ADDR_RGB_MODE);
    if (val <= 11)
        rgbMode = val;
    else
        rgbMode = 11;  // 无效值默认自动模式
}

void DummyRobot::SaveRGBModeToEEPROM()
{
    eeprom.write(EEPROM_ADDR_RGB_MODE, (uint8_t)rgbMode);
}

void DummyRobot::SetRGBAutoSubState(uint8_t subState)
{
    rgb.autoSubState = (RGB::AutoSubState)subState;
    // 重置效果计步器，确保新效果从头开始
    rgb.ResetEffectSteps();
    // 切到工作/完成状态时重置空闲计时
    if (subState == RGB::AUTO_WORKING || subState == RGB::AUTO_COMPLETE)
        autoIdleTickMs = 0;
}

void DummyRobot::NotifyWorkStart()
{
    if (rgbMode == RGB::MODE_AUTO && robotState == STATE_NORMAL)
        SetRGBAutoSubState(RGB::AUTO_WORKING);
}

void DummyRobot::NotifyWorkDone()
{
    // 运动正常完成：清除 pending（避免下次 ESTOP 时误复放已完成的运动）
    if (robotState == STATE_NORMAL)
        hasPendingResumeTarget = false;

    if (rgbMode == RGB::MODE_AUTO && robotState == STATE_NORMAL)
    {
        SetRGBAutoSubState(RGB::AUTO_COMPLETE);
        autoIdleTickMs = 0;  // 开始 5 秒倒计时
    }
}

void DummyRobot::AutoModeIdleTick(uint32_t deltaMs)
{
    if (rgbMode != RGB::MODE_AUTO)
        return;

    // 工作状态 + 运动已停止 → 防抖 100ms 后自动切完成态
    // （解决可中断模式下无人调用 NotifyWorkDone 的问题）
    if (rgb.autoSubState == RGB::AUTO_WORKING)
    {
        if (!IsMoving() && IsEnabled())
        {
            autoIdleTickMs += deltaMs;
            if (autoIdleTickMs >= 100)
            {
                SetRGBAutoSubState(RGB::AUTO_COMPLETE);
                autoIdleTickMs = 0;
            }
        }
        else
        {
            autoIdleTickMs = 0;
        }
        return;
    }

    // 完成态 5 秒回待机
    if (rgb.autoSubState == RGB::AUTO_COMPLETE)
    {
        autoIdleTickMs += deltaMs;
        if (autoIdleTickMs >= AUTO_IDLE_THRESHOLD_MS)
        {
            SetRGBAutoSubState(RGB::AUTO_STANDBY);
            autoIdleTickMs = 0;
        }
    }
}

// ==================== 在线检测 ====================

bool DummyRobot::CheckNodeOnline(uint8_t node)
{
    if (node >= 1 && node <= 6)
    {
        motorJ[node]->QueryOnline();
        osDelay(50);  // 等待 CAN 回调
        return motorJ[node]->isOnline;
    }
    else if (node == 7 && hand2 != nullptr)
    {
        hand2->QueryOnline();
        osDelay(50);
        return hand2->isOnline;
    }
    return false;
}

bool DummyRobot::CheckAllNodesOnline()
{
    bool all = true;
    for (int n = 1; n <= 6; n++)
    {
        motorJ[n]->QueryOnline();
    }
    if (hand2 != nullptr)
        hand2->QueryOnline();

    osDelay(80);  // 统一等待所有回调

    for (int n = 1; n <= 6; n++)
    {
        if (!motorJ[n]->isOnline)
            all = false;
    }
    if (hand2 != nullptr && !hand2->isOnline)
        all = false;

    allNodesOnline = all;
    return all;
}

void DummyRobot::OnlineCheckTick(uint32_t deltaMs)
{
    // 工作时不检测
    if (robotState != STATE_NORMAL || IsMoving())
        return;

    onlineCheckAccumMs += deltaMs;
    if (onlineCheckAccumMs >= ONLINE_CHECK_INTERVAL_MS)
    {
        onlineCheckAccumMs = 0;
        CheckAllNodesOnline();
    }
}

void DummyRobot::UpdateJointPose6D()
{
    dof6Solver->SolveFK(currentJoints, currentPose6D);
    currentPose6D.X *= 1000; // m -> mm
    currentPose6D.Y *= 1000; // m -> mm
    currentPose6D.Z *= 1000; // m -> mm
}


bool DummyRobot::IsMoving()
{
    return jointsStateFlag != 0b1111110;
}


bool DummyRobot::IsEnabled()
{
    return isEnabled;
}


void DummyRobot::SetCommandMode(uint32_t _mode)
{
    if (_mode < COMMAND_TARGET_POINT_SEQUENTIAL ||
        _mode > COMMAND_MOTOR_TUNING)
        return;

    commandMode = static_cast<CommandMode>(_mode);

    switch (commandMode)
    {
        case COMMAND_TARGET_POINT_SEQUENTIAL:
        case COMMAND_TARGET_POINT_INTERRUPTABLE:
            jointSpeedRatio = 1;
            SetJointAcceleration(DEFAULT_JOINT_ACCELERATION_LOW);
            break;
        case COMMAND_CONTINUES_TRAJECTORY:
            SetJointAcceleration(DEFAULT_JOINT_ACCELERATION_HIGH);
            jointSpeedRatio = 0.3;
            break;
        case COMMAND_MOTOR_TUNING:
            break;
    }
}


uint32_t DummyRobot::CommandHandler::Push(const std::string &_cmd)
{
    // 急停/暂停状态下拒绝新的运动命令进队
    if (context->robotState != STATE_NORMAL)
        return 0xFE; // rejected by state
    // 未使能拒绝
    if (!context->IsEnabled())
        return 0xFD; // rejected: not enabled
    // 未 home 拒绝（避免撞机）
    if (!context->IsHomed())
        return 0xFC; // rejected: not homed

    osStatus_t status = osMessageQueuePut(commandFifo, _cmd.c_str(), 0U, 0U);
    if (status == osOK)
        return osMessageQueueGetSpace(commandFifo);

    return 0xFF; // failed
}


void DummyRobot::CommandHandler::EmergencyStop()
{
    // 1. 广播 CAN（nodeID=0）全轴同步进入速度环，目标速度=0 -> 机械制动
    context->motorJ[ALL]->SetVelocitySetPoint(0);

    // 2. 同步软件目标点为当前位置，避免后续 FixUpdate 恢复位置环时跳动
    context->UpdateJointAngles();
    for (int j = 0; j < 6; j++)
        context->targetJoints.a[j] = context->currentJoints.a[j];

    // 3. 强制前先检查是否有活动运动——无运动则清除 pending（避免误复放）
    // 必须在 jointsStateFlag 被强制之前读，否则 IsMoving() 永远返回 false
    if (!context->IsMoving())
        context->hasPendingResumeTarget = false;
    // 还有 pending 则由 MoveJ/Homing/Resting 设好了 stoppedJointSpeed，这里不导致覆盖

    // 4. 强制 motor state 为 FINISH，避免 IsMoving() 永久卡住
    for (int i = 1; i <= 6; i++)
        context->motorJ[i]->state = CtrlStepMotor::FINISH;
    context->jointsStateFlag = 0b1111110;

    // 5. 进入急停状态（保持使能、保留队列）
    context->robotState = STATE_EMERGENCY_STOP;
    // 6. RGB 自动模式：急停 → 红色快闪
    context->SetRGBAutoSubState(RGB::AUTO_ESTOP);
}


bool DummyRobot::CommandHandler::Pause()
{
    // 仅允许 NORMAL → PAUSED。ESTOP/PAUSED 下拒绝（已经停止或已暂停）。
    if (context->robotState != STATE_NORMAL)
        return false;

    // 软暂停语义：不广播 V=0、不同步 target——让当前动作自然完成。
    // 只设状态。未来从队列出来的命令会在 ParseCommand 入口被阻住；
    // 新的 Push 会被拒绝。
    context->stoppedJointSpeed = context->jointSpeed;
    context->robotState = STATE_PAUSED;
    // RGB 自动模式：暂停 → 黄色流水灯
    context->SetRGBAutoSubState(RGB::AUTO_PAUSED);
    return true;
}


void DummyRobot::CommandHandler::Resume()
{
    if (context->robotState == STATE_EMERGENCY_STOP ||
        context->robotState == STATE_PAUSED)
    {
        bool wasEstop = (context->robotState == STATE_EMERGENCY_STOP);
        bool hasPending = context->hasPendingResumeTarget;

        // ★ 必须先切回 NORMAL，再设置运动目标！
        // 否则 FixUpdate 急停分支会持续把 targetJoints 覆盖为 currentJoints，
        // 导致 MoveJ 设置的目标被立即冲掉，RESUME 后不运动。
        float queueSpeed = context->stoppedJointSpeed;   // 用户的队列速度（需在 MoveJ 前保存）
        context->jointSpeed = queueSpeed;
        context->robotState = STATE_NORMAL;

        // 急停中断的内联运动（!HOME/!RESET）还原目标
        if (wasEstop && hasPending)
        {
            context->jointSpeed = context->pendingResumeSpeed;
            context->MoveJ(context->pendingResumeTarget.a[0],
                           context->pendingResumeTarget.a[1],
                           context->pendingResumeTarget.a[2],
                           context->pendingResumeTarget.a[3],
                           context->pendingResumeTarget.a[4],
                           context->pendingResumeTarget.a[5]);
            // ★ 立即向电机发送位置命令，不等 FixUpdate 调度，
            // 避免 FixUpdate 急停分支偷跑一次覆盖 targetJoints。
            context->MoveJoints(context->targetJoints);

            // MoveJ 已自动重新预置 pending（供下次 STOP 时继续续跑），不要在此清除！
            // pending 会由 NotifyWorkDone（正常完成）或下次 EmergencyStop（空闲态）清除。

            // MoveJ 同时把 stoppedJointSpeed 覆盖成 pendingResumeSpeed，还原为用户队列速度
            context->stoppedJointSpeed = queueSpeed;
            context->jointSpeed = queueSpeed;
        }

        // RGB：恢复后进入工作态，等待队列执行完成后自动 → 完成 → 待机
        context->SetRGBAutoSubState(RGB::AUTO_WORKING);
    }
}


std::string DummyRobot::CommandHandler::Pop(uint32_t timeout)
{
    osStatus_t status = osMessageQueueGet(commandFifo, strBuffer, nullptr, timeout);

    return std::string{strBuffer};
}


uint32_t DummyRobot::CommandHandler::GetSpace()
{
    return osMessageQueueGetSpace(commandFifo);
}


uint32_t DummyRobot::CommandHandler::ParseCommand(const std::string &_cmd)
{
    uint8_t argNum;

    // 急停/暂停期间阻塞当前 Pop 出的命令，等待 RESUME
    // 这样命令不会丢失，队列中后续命令也保留
    while (context->robotState != STATE_NORMAL)
        osDelay(10);

    switch (context->commandMode)
    {
        case COMMAND_TARGET_POINT_SEQUENTIAL:
        case COMMAND_CONTINUES_TRAJECTORY:
            if (_cmd[0] == '>' || _cmd[0] == '&')
            {
                float joints[6];
                float speed;

                if (_cmd[0] == '>')
                    argNum = sscanf(_cmd.c_str(), ">%f,%f,%f,%f,%f,%f,%f", joints, joints + 1, joints + 2,
                                    joints + 3, joints + 4, joints + 5, &speed);
                if (_cmd[0] == '&')
                    argNum = sscanf(_cmd.c_str(), "&%f,%f,%f,%f,%f,%f,%f", joints, joints + 1, joints + 2,
                                    joints + 3, joints + 4, joints + 5, &speed);
                if (argNum == 6)
                {
                    context->MoveJ(joints[0], joints[1], joints[2],
                                   joints[3], joints[4], joints[5]);
                } else if (argNum == 7)
                {
                    context->SetJointSpeed(speed);
                    context->MoveJ(joints[0], joints[1], joints[2],
                                   joints[3], joints[4], joints[5]);
                }
                // Trigger a transmission immediately, in case IsMoving() returns false
                context->MoveJoints(context->targetJoints);
                context->NotifyWorkStart();

                while (context->IsMoving() && context->IsEnabled())
                {
                    if (context->robotState == STATE_EMERGENCY_STOP) break; // 仅急停中断；PAUSE 让当前点运动完成
                    osDelay(5);
                }
                context->NotifyWorkDone();
                Respond(*usbStreamOutputPtr, "Move OK");
                Respond(*uart4StreamOutputPtr, "Move OK");
            } else if (_cmd[0] == '@')
            {
                float pose[6];
                float speed;

                argNum = sscanf(_cmd.c_str(), "@%f,%f,%f,%f,%f,%f,%f", pose, pose + 1, pose + 2,
                                pose + 3, pose + 4, pose + 5, &speed);
                if (argNum == 6)
                {
                    context->MoveL(pose[0], pose[1], pose[2], pose[3], pose[4], pose[5]);
                } else if (argNum == 7)
                {
                    context->SetJointSpeed(speed);
                    context->MoveL(pose[0], pose[1], pose[2], pose[3], pose[4], pose[5]);
                }
                context->NotifyWorkStart();
                context->NotifyWorkDone();
                Respond(*usbStreamOutputPtr, "Move OK");
                Respond(*uart4StreamOutputPtr, "Move OK");
            }

            break;

        case COMMAND_TARGET_POINT_INTERRUPTABLE:
            if (_cmd[0] == '>' || _cmd[0] == '&')
            {
                float joints[6];
                float speed;

                if (_cmd[0] == '>')
                    argNum = sscanf(_cmd.c_str(), ">%f,%f,%f,%f,%f,%f,%f", joints, joints + 1, joints + 2,
                                    joints + 3, joints + 4, joints + 5, &speed);
                if (_cmd[0] == '&')
                    argNum = sscanf(_cmd.c_str(), "&%f,%f,%f,%f,%f,%f,%f", joints, joints + 1, joints + 2,
                                    joints + 3, joints + 4, joints + 5, &speed);
                bool ok = false;
                if (argNum == 6)
                {
                    ok = context->MoveJ(joints[0], joints[1], joints[2],
                                        joints[3], joints[4], joints[5]);
                } else if (argNum == 7)
                {
                    context->SetJointSpeed(speed);
                    ok = context->MoveJ(joints[0], joints[1], joints[2],
                                        joints[3], joints[4], joints[5]);
                }
                if (ok) {
                    context->NotifyWorkStart();
                    Respond(*usbStreamOutputPtr, "Move OK");
                    Respond(*uart4StreamOutputPtr, "Move OK");
                } else {
                    // 6 关节通用越限提示：列出每个越限关节及方向 (Max/Min)
                    char buf[80]; size_t off = 0; buf[0] = '\0';
                    for (int j = 1; j <= 6 && off < sizeof(buf) - 1; j++) {
                        float v = joints[j-1];
                        if (v > context->motorJ[j]->angleLimitMax) {
                            off += snprintf(buf + off, sizeof(buf) - off, "J%d Max ", j);
                        } else if (v < context->motorJ[j]->angleLimitMin) {
                            off += snprintf(buf + off, sizeof(buf) - off, "J%d Min ", j);
                        }
                    }
                    Respond(*usbStreamOutputPtr, "Move Error: limit %s", buf);
                    Respond(*uart4StreamOutputPtr, "Move Error: limit %s", buf);
                }
            } else if (_cmd[0] == '@')
            {
                float pose[6];
                float speed;

                argNum = sscanf(_cmd.c_str(), "@%f,%f,%f,%f,%f,%f,%f", pose, pose + 1, pose + 2,
                                pose + 3, pose + 4, pose + 5, &speed);
                bool ok = false;
                if (argNum == 6)
                {
                    ok = context->MoveL(pose[0], pose[1], pose[2], pose[3], pose[4], pose[5]);
                } else if (argNum == 7)
                {
                    context->SetJointSpeed(speed);
                    ok = context->MoveL(pose[0], pose[1], pose[2], pose[3], pose[4], pose[5]);
                }
                if (ok) {
                    context->NotifyWorkStart();
                    Respond(*usbStreamOutputPtr, "Move OK");
                    Respond(*uart4StreamOutputPtr, "Move OK");
                } else {
                    Respond(*usbStreamOutputPtr, "Move Error: unreachable");
                    Respond(*uart4StreamOutputPtr, "Move Error: unreachable");
                }
            }
            break;

        case COMMAND_MOTOR_TUNING:
            break;
    }

    return osMessageQueueGetSpace(commandFifo);
}


void DummyRobot::CommandHandler::ClearFifo()
{
    osMessageQueueReset(commandFifo);
}


void DummyRobot::TuningHelper::SetTuningFlag(uint8_t _flag)
{
    tuningFlag = _flag;
}


void DummyRobot::TuningHelper::Tick(uint32_t _timeMillis)
{
    time += PI * 2 * frequency * (float) _timeMillis / 1000.0f;
    float delta = amplitude * sinf(time);

    for (int i = 1; i <= 6; i++)
        if (tuningFlag & (1 << (i - 1)))
            context->motorJ[i]->SetAngle(delta);
}


void DummyRobot::TuningHelper::SetFreqAndAmp(float _freq, float _amp)
{
    if (_freq > 5)_freq = 5;
    else if (_freq < 0.1) _freq = 0.1;
    if (_amp > 50)_amp = 50;
    else if (_amp < 1) _amp = 1;

    frequency = _freq;
    amplitude = _amp;
}
