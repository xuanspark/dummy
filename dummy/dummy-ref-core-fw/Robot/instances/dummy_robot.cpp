#include "communication.hpp"
#include "dummy_robot.h"

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

    MoveJ(0, 0, 90, 0, 0, 0);
    MoveJoints(targetJoints);
    while (IsMoving())
    {
        if (robotState == STATE_EMERGENCY_STOP)
        {
            // 记录原目标和当前速度供 RESUME 续跑
            pendingResumeTarget = {0, 0, 90, 0, 0, 0};
            pendingResumeSpeed = jointSpeed;
            hasPendingResumeTarget = true;
            // 覆盖 stoppedJointSpeed 为 Homing 前的队列速度（而非 Homing 低速）
            stoppedJointSpeed = lastSpeed;
            break;
        }
        osDelay(10);
    }

    SetJointSpeed(lastSpeed);
    // 标记已 home（含 STOP-中断后 RESUME 续跑场景：
    // RESUME 后 FixUpdate 会驱动到 home，期间 robotState != NORMAL，运动命令仍被门控）
    isHomed = true;
}


void DummyRobot::Resting()
{
    float lastSpeed = jointSpeed;
    SetJointSpeed(10);

    MoveJ(REST_POSE.a[0], REST_POSE.a[1], REST_POSE.a[2],
          REST_POSE.a[3], REST_POSE.a[4], REST_POSE.a[5]);
    MoveJoints(targetJoints);
    while (IsMoving())
    {
        if (robotState == STATE_EMERGENCY_STOP)
        {
            // 记录原目标和当前速度供 RESUME 续跑
            pendingResumeTarget = REST_POSE;
            pendingResumeSpeed = jointSpeed;
            hasPendingResumeTarget = true;
            // 覆盖 stoppedJointSpeed 为 Resting 前的队列速度（而非 Resting 低速）
            stoppedJointSpeed = lastSpeed;
            break;
        }
        osDelay(10);
    }

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
    // !START：使能机械臂 + 使能手爪 + 回零点姿态（REST_POSE）
    SetEnable(true);
    if (hand2 != nullptr)
        hand2->SetEnable(true);
    Resting();
    // 零点姿态（REST_POSE）不是工作姿态（home），运动前仍需 !HOME
    isHomed = false;
}


void DummyRobot::Shutdown()
{
    // !CLOSE：先回零点姿态，再失能（先回零避免失能后重力倒伏危险）
    if (isEnabled)
        Resting();
    if (hand2 != nullptr)
        hand2->SetEnable(false);
    SetEnable(false);  // 内部会清 isHomed
}


void DummyRobot::OnHandActionRGBHook(bool _open)
{
    // TODO: 后续接入 RGB 具体动效（开手爪绿保持 / 闭手爪红保持）。
    // 预留接口，当前仅作为调用点。
    (void)_open;
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
}

uint32_t DummyRobot::GetRGBMode()
{
    return rgbMode;
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

    // 3. 强制 motor state 为 FINISH，避免 IsMoving() 永久卡住
    for (int i = 1; i <= 6; i++)
        context->motorJ[i]->state = CtrlStepMotor::FINISH;
    context->jointsStateFlag = 0b1111110;

    // 4. 冻结当前速度，供 RESUME 还原
    context->stoppedJointSpeed = context->jointSpeed;

    // 5. 进入急停状态（保持使能、保留队列）
    context->robotState = STATE_EMERGENCY_STOP;
}


void DummyRobot::CommandHandler::Pause()
{
    // 软暂停语义：不广播 V=0、不同步 target——让当前动作自然完成。
    // 只设状态。未来从队列出来的命令会在 ParseCommand 入口被阻住；
    // 新的 Push 会被拒绝。
    context->stoppedJointSpeed = context->jointSpeed;
    context->robotState = STATE_PAUSED;
}


void DummyRobot::CommandHandler::Resume()
{
    if (context->robotState == STATE_EMERGENCY_STOP ||
        context->robotState == STATE_PAUSED)
    {
        // 急停中断的内联运动（!HOME/!RESET）还原目标，RESUME 后 FixUpdate 续跑
        // 用 MoveJ 重算 dynamicJointSpeeds（基于停止时的实际当前位置 -> 原目标）
        if (context->robotState == STATE_EMERGENCY_STOP &&
            context->hasPendingResumeTarget)
        {
            // 用内联运动被中断时的速度续跑到原目标
            context->jointSpeed = context->pendingResumeSpeed;
            context->MoveJ(context->pendingResumeTarget.a[0],
                           context->pendingResumeTarget.a[1],
                           context->pendingResumeTarget.a[2],
                           context->pendingResumeTarget.a[3],
                           context->pendingResumeTarget.a[4],
                           context->pendingResumeTarget.a[5]);
            context->hasPendingResumeTarget = false;
        }
        // 统一还原急停/暂停前冻结的队列速度
        context->jointSpeed = context->stoppedJointSpeed;
        context->robotState = STATE_NORMAL;
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

                while (context->IsMoving() && context->IsEnabled())
                {
                    if (context->robotState == STATE_EMERGENCY_STOP) break; // 仅急停中断；PAUSE 让当前点运动完成
                    osDelay(5);
                }
                Respond(*usbStreamOutputPtr, "ok");
                Respond(*uart4StreamOutputPtr, "ok");
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
                Respond(*usbStreamOutputPtr, "ok");
                Respond(*uart4StreamOutputPtr, "ok");
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
                Respond(*usbStreamOutputPtr, "ok");
                Respond(*uart4StreamOutputPtr, "ok");
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
                Respond(*usbStreamOutputPtr, "ok");
                Respond(*uart4StreamOutputPtr, "ok");
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
