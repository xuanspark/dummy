#include "communication.hpp"
#include "dummy_robot.h"
#include "eeprom_interface.h"
#include <cmath>
#include <cstring>

extern RGB rgb;
static EEPROMClass eeprom;
static osThreadId_t emergencyTask;

static StreamSink& Output(uint8_t channel)
{
    if (channel == StreamSink::CHANNEL_TYPE_USB) return *usbStreamOutputPtr;
    if (channel == StreamSink::CHANNEL_TYPE_UART5) return *uart5StreamOutputPtr;
    return *uart4StreamOutputPtr;
}

DummyRobot::DummyRobot(CAN_HandleTypeDef* bus) : hcan(bus)
{
    motorJ[0] = new CtrlStepMotor(bus, 0, false, 1, -180, 180);
    motorJ[1] = new CtrlStepMotor(bus, 1, false, 50, -170, 170);
    motorJ[2] = new CtrlStepMotor(bus, 2, true, 50, -75, 90);
    motorJ[3] = new CtrlStepMotor(bus, 3, true, 50, 35, 180);
    motorJ[4] = new CtrlStepMotor(bus, 4, true, 50, -180, 180);
    motorJ[5] = new CtrlStepMotor(bus, 5, true, 50, -100, 100);
    motorJ[6] = new CtrlStepMotor(bus, 6, true, 30, -180, 180);
    motorJ[7] = hand2 = new StepHand(bus, 7);
    dof6Solver = new DOF6Kinematic(0.134f, 0.035f, 0.146f, 0.117f, 0.052f, 0.0625f);
}
DummyRobot::~DummyRobot()
{
    for (int n = 0; n <= 6; ++n) delete motorJ[n];
    delete hand2;
    delete dof6Solver;
}
void DummyRobot::Init()
{
    if (!commandHandler.IsReady())
    {
        HandleRuntimeInitFailure();
    }
    SetCommandMode(DEFAULT_COMMAND_MODE);
    SetJointSpeed(DEFAULT_JOINT_SPEED);
}
const char* DummyRobot::StateName() const
{
    switch (robotState) {
        case STATE_EMERGENCY_STOP: return "EMERGENCY_STOP";
        case STATE_PAUSED: return "PAUSED";
        default: return "NORMAL";
    }
}

// 中断只锁存状态并唤醒最高优先级任务，不在中断内等待 CAN 或打印。
void DummyRobot::CaptureEmergencyHoldPose()
{
    for (int n = 1; n <= 6; ++n) {
        emergencyHoldPose.a[n - 1] = motorJ[n]->angle;
    }
}
void DummyRobot::RequestEmergencyStopFromISR()
{
    if (robotState != STATE_EMERGENCY_STOP) {
        restoreEnablePending = enableTransitionActive;
        CaptureEmergencyHoldPose();
        ++stopEpoch;
        emergencyHoldActive = true;
        emergencyBrakeEpoch = 0xffffffffu;
        robotState = STATE_EMERGENCY_STOP;
    }
}
void DummyRobot::RequestEmergencyStop()
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (robotState != STATE_EMERGENCY_STOP) {
        restoreEnablePending = enableTransitionActive;
        CaptureEmergencyHoldPose();
        ++stopEpoch;
        emergencyHoldActive = true;
        emergencyBrakeEpoch = 0xffffffffu;
        robotState = STATE_EMERGENCY_STOP;
    }
    __set_PRIMASK(mask);
    if (emergencyTask) osThreadFlagsSet(emergencyTask, 1);
}
void DummyRobot::ApplyEmergencyBrake()
{
    if (robotState != STATE_EMERGENCY_STOP) return;
    if (restoreEnablePending && motorJ[ALL]->SetEnable(true)) {
        isEnabled = true;
        hand2->enabledFlag = true;
        restoreEnablePending = false;
    }
    // 未启动或已正常关闭时不切换电机模式，避免零速度报文把失能电机重新唤醒。
    if (!isEnabled) return;
    // 广播速度零覆盖关节和手爪；保持原有使能，不改写待恢复目标。
    motorJ[ALL]->SetVelocitySetPoint(0);
    if (emergencyBrakeEpoch == stopEpoch) return;
    for (int n = 1; n <= 6; ++n) {
        const float hold = emergencyHoldPose.a[n - 1];
        if (std::isfinite(hold)) motorJ[n]->SetAngleWithVelocityLimit(hold, 1.0f);
    }
    emergencyBrakeEpoch = stopEpoch;
}
bool DummyRobot::CanTransmit(uint8_t command, const uint8_t* data) const
{
    if (command >= 0x20 && command <= 0x2b) return true;
    if (robotState == STATE_EMERGENCY_STOP) {
        if (command == 0x01 && restoreEnablePending) {
            uint32_t enabled = 0;
            std::memcpy(&enabled, data, sizeof(enabled));
            return enabled == 1;
        }
        if (command == 0x07 && emergencyHoldActive) return true;
        float value = 1;
        std::memcpy(&value, data, sizeof(value));
        return command == 0x04 && value == 0;
    }
    if (command == 0x01) {
        uint32_t enabled;
        std::memcpy(&enabled, data, sizeof(enabled));
        return robotState == STATE_NORMAL && (enabled != 0 || disableAuthorized || handDisableAuthorized);
    }
    // 暂停允许当前任务的内部动作自然完成；入口拒绝所有新业务。
    return robotState == STATE_NORMAL || taskActive;
}
static void EmergencyThread(void* argument)
{
    auto* robot = static_cast<DummyRobot*>(argument);
    for (;;) {
        osThreadFlagsWait(1, osFlagsWaitAny, osWaitForever);
        robot->ApplyEmergencyBrake();
    }
}
void DummyRobot::InitSysCmdThread()
{
    // 系统动作复用运动队列，仅保留一个独立的急停任务。
    const osThreadAttr_t attr = {
        .name = "EmergencyTask",
        .stack_size = 1024,
        .priority = osPriorityRealtime7,
    };
    emergencyTask = osThreadNew(EmergencyThread, this, &attr);
    if (!emergencyTask)
    {
        HandleRuntimeInitFailure();
    }
}
bool DummyRobot::PostSysCmd(SysCmd command, uint8_t channel)
{
    const char* names[] = {"", "!START", "!HOME", "!RESET", "!CLOSE"};
    return command >= SYSCMD_STARTUP && command <= SYSCMD_SHUTDOWN &&
           commandHandler.Push(names[command], channel) < 0xfb;
}
void DummyRobot::CommandHandler::EmergencyStop()
{
    context->RequestEmergencyStop();
    context->SetRGBAutoSubState(RGB::AUTO_ESTOP);
}
bool DummyRobot::CommandHandler::Pause()
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    bool accepted = context->robotState != STATE_EMERGENCY_STOP;
    if (accepted) context->robotState = STATE_PAUSED;
    __set_PRIMASK(mask);
    if (accepted) context->SetRGBAutoSubState(RGB::AUTO_PAUSED);
    return accepted;
}
void DummyRobot::CommandHandler::Resume()
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    context->robotState = STATE_NORMAL;
    context->emergencyHoldActive = false;
    context->emergencyBrakeEpoch = 0xffffffffu;
    __set_PRIMASK(mask);
    context->SetRGBAutoSubState(context->taskActive ? RGB::AUTO_WORKING : RGB::AUTO_STANDBY);
}

bool DummyRobot::WaitReady()
{
    while (robotState == STATE_EMERGENCY_STOP || (robotState == STATE_PAUSED && !taskActive)) osDelay(5);
    return IsEnabled();
}
bool DummyRobot::SetEnable(bool enabled)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (robotState != STATE_NORMAL || (!enabled && !disableAuthorized)) {
        __set_PRIMASK(mask);
        return false;
    }
    uint32_t epoch = stopEpoch;
    enableTransitionActive = true;
    __set_PRIMASK(mask);
    // 广播节点覆盖所有关节和手爪，避免两次使能发送之间被 STOP 抢占。
    bool sent = motorJ[ALL]->SetEnable(enabled);
    mask = __get_PRIMASK();
    __disable_irq();
    bool accepted = sent && epoch == stopEpoch && robotState == STATE_NORMAL;
    if (accepted) {
        isEnabled = enabled;
        hand2->enabledFlag = enabled;
        if (!enabled) isHomed = false;
    }
    enableTransitionActive = false;
    __set_PRIMASK(mask);
    return accepted;
}
bool DummyRobot::Disable()
{
    // 失能授权仅在实际回零完成后短暂打开，不能通过协议直接调用。
    if (robotState != STATE_NORMAL || armMotionActive) return false;
    for (int n = 1; n <= 6; ++n) {
        if (motorJ[n]->angleSequence == 0 || HAL_GetTick() - motorJ[n]->lastAngleTick > 250 ||
            !std::isfinite(currentJoints.a[n - 1]) ||
            std::fabs(currentJoints.a[n - 1] - REST_POSE.a[n - 1]) > 0.5f ||
            motorJ[n]->state != CtrlStepMotor::FINISH) return false;
    }
    // 急停中断不能插入“检查”和“失能”之间；总线层再次检查授权及状态。
    disableAuthorized = true;
    bool disabled = SetEnable(false);
    disableAuthorized = false;
    return disabled && !isEnabled;
}
bool DummyRobot::SetHandEnable(bool enabled)
{
    if (robotState != STATE_NORMAL || !IsEnabled() || armMotionActive) return false;
    if (enabled) {
        if (hand2->isEnabled()) return true;
        const bool result = hand2->SetEnable(true);
        if (result) {
            hand2->enabledFlag = true;
            SetRGBAutoSubState(RGB::AUTO_WORKING);
        }
        return result;
    }
    if (!hand2->isEnabled()) return true;
    if (hand2->state == CtrlStepMotor::RUNNING) return false;
    handDisableAuthorized = true;
    const bool result = hand2->SetEnable(false);
    handDisableAuthorized = false;
    if (result) {
        hand2->enabledFlag = false;
        SetRGBAutoSubState(RGB::AUTO_STANDBY);
    }
    return result;
}
bool DummyRobot::Startup()
{
    while (robotState != STATE_NORMAL) osDelay(5);
    if (!CheckAllNodesOnline()) return false;
    while (robotState != STATE_NORMAL) osDelay(5);
    bool enabled = SetEnable(true);
    while (robotState != STATE_NORMAL) osDelay(5);
    if (!enabled && !SetEnable(true)) return false;
    isHomed = false;
    // START 只使能手爪，不附带意外闭合动作。
    return Resting();
}
bool DummyRobot::Shutdown()
{
    if (!IsEnabled()) return false;
    for (;;) {
        uint32_t epoch = stopEpoch;
        if (!Resting()) return false;
        while (robotState != STATE_NORMAL) osDelay(5);
        if (Disable()) return true;
        if (epoch == stopEpoch && robotState == STATE_NORMAL) return false;
        // 回零结束与失能发送之间发生 STOP 时，恢复后重新校验回零再失能。
    }
}
bool DummyRobot::Homing()
{
    if (!IsEnabled()) return false;
    if (!RunPose(DOF6Kinematic::Joint6D_t(0, 0, 90, 0, 0, 0), 10)) return false;
    isHomed = true;
    return true;
}
bool DummyRobot::Resting()
{
    if (!IsEnabled()) return false;
    bool complete = RunPose(REST_POSE, 10);
    if (complete) isHomed = false;
    return complete;
}
bool DummyRobot::EnsureHomed() { return IsEnabled() && (isHomed || Homing()); }

bool DummyRobot::MoveJ(float j1, float j2, float j3, float j4, float j5, float j6)
{
    if (!IsEnabled() || !std::isfinite(jointSpeed) || jointSpeed <= 0)
        return false;
    DOF6Kinematic::Joint6D_t target(j1, j2, j3, j4, j5, j6);
    float maximumTime = 0;
    for (int n = 1; n <= 6; ++n) {
        float value = target.a[n - 1];
        if (!std::isfinite(value) || value < motorJ[n]->angleLimitMin || value > motorJ[n]->angleLimitMax)
            return false;
        maximumTime = fmaxf(maximumTime, fabsf(value - currentJoints.a[n - 1]) * motorJ[n]->reduction / jointSpeed);
    }
    for (int n = 1; n <= 6; ++n) {
        float distance = fabsf(target.a[n - 1] - currentJoints.a[n - 1]);
        dynamicJointSpeeds.a[n - 1] = maximumTime > 0.0001f ?
            fmaxf(0.01f, distance * motorJ[n]->reduction / maximumTime * 0.1f) : 0.01f;
    }
    targetJoints = target;
    pendingResumeTarget = target;
    pendingResumeSpeed = jointSpeed;
    hasPendingResumeTarget = true;
    jointsStateFlag = 0;
    return true;
}
bool DummyRobot::MoveL(float x, float y, float z, float a, float b, float c)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c)) return false;
    DOF6Kinematic::Pose6D_t pose(x, y, z, a, b, c);
    DOF6Kinematic::IKSolves_t solutions{};
    DOF6Kinematic::Joint6D_t last = currentJoints;
    dof6Solver->SolveIK(pose, last, solutions);
    float best = INFINITY;
    int selected = -1;
    for (int i = 0; i < 8; ++i) {
        float distance = 0;
        bool valid = true;
        for (int n = 1; n <= 6; ++n) {
            float value = solutions.config[i].a[n - 1];
            if (!std::isfinite(value) || value < motorJ[n]->angleLimitMin || value > motorJ[n]->angleLimitMax)
                valid = false;
            distance = fmaxf(distance, fabsf(value - currentJoints.a[n - 1]));
        }
        if (valid && distance < best) { best = distance; selected = i; }
    }
    if (selected < 0) return false;
    auto& target = solutions.config[selected];
    return MoveJ(target.a[0], target.a[1], target.a[2], target.a[3], target.a[4], target.a[5]);
}
void DummyRobot::MoveJoints(DOF6Kinematic::Joint6D_t pose)
{
    if (!IsEnabled() || !armMotionActive || robotState == STATE_EMERGENCY_STOP) return;
    for (int n = 1; n <= 6; ++n) {
        if (robotState == STATE_EMERGENCY_STOP) return;
        motorJ[n]->SetAngleWithVelocityLimit(pose.a[n - 1] - initPose.a[n - 1], dynamicJointSpeeds.a[n - 1]);
    }
}
bool DummyRobot::TryMotionFault(const char* reason, uint8_t node, float error, uint32_t epoch)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (epoch != stopEpoch || robotState == STATE_EMERGENCY_STOP) {
        __set_PRIMASK(mask);
        return false;
    }
    auto* motor = motorJ[node];
    motionFault = {reason, node, motor->angleSequence ? HAL_GetTick() - motor->lastAngleTick : 0xffffffffu,
                   error, motor->state == CtrlStepMotor::FINISH, motor->isStalled};
    RequestEmergencyStop();
    __set_PRIMASK(mask);
    SetRGBAutoSubState(RGB::AUTO_ESTOP);
    return true;
}

bool DummyRobot::RunPose(const DOF6Kinematic::Joint6D_t& pose, float speed)
{
    if (!WaitReady()) return false;
    const float previousSpeed = jointSpeed;
    uint32_t seenStop = stopEpoch;
    uint32_t sequences[6];
    for (int n = 1; n <= 6; ++n) sequences[n - 1] = motorJ[n]->angleSequence;
    SetJointSpeed(speed);
    if (!MoveJ(pose.a[0], pose.a[1], pose.a[2], pose.a[3], pose.a[4], pose.a[5])) {
        jointSpeed = previousSpeed;
        return false;
    }
    armMotionActive = true;
    uint32_t activeTime = 0;
    for (;;) {
        if (robotState == STATE_EMERGENCY_STOP || seenStop != stopEpoch) {
            while (robotState != STATE_NORMAL) osDelay(5);
            seenStop = stopEpoch;
            // 急停期间不计入动作超时；恢复后从零开始统计本次实际执行时间。
            activeTime = 0;
            SetJointSpeed(speed);
            if (!MoveJ(pose.a[0], pose.a[1], pose.a[2], pose.a[3], pose.a[4], pose.a[5])) break;
            for (int n = 1; n <= 6; ++n) sequences[n - 1] = motorJ[n]->angleSequence;
        }
        MoveJoints(targetJoints);
        bool complete = true;
        uint8_t failedNode = 1;
        const char* failure = "arm_not_arrived";
        for (int n = 1; n <= 6; ++n) {
            auto* motor = motorJ[n];
            if (motor->angleSequence == sequences[n - 1] || HAL_GetTick() - motor->lastAngleTick > 250 ||
                fabsf(currentJoints.a[n - 1] - pose.a[n - 1]) > 0.5f || motor->state != CtrlStepMotor::FINISH) {
                complete = false;
                failedNode = n;
                failure = motor->angleSequence == sequences[n - 1] || HAL_GetTick() - motor->lastAngleTick > 250 ?
                    "arm_feedback_timeout" : "arm_not_arrived";
            }
        }
        if (complete && seenStop == stopEpoch && robotState != STATE_EMERGENCY_STOP) {
            armMotionActive = false;
            hasPendingResumeTarget = false;
            jointSpeed = previousSpeed;
            return true;
        }
        if ((!IsEnabled() || activeTime >= 120000) &&
            TryMotionFault(IsEnabled() ? failure : "arm_disabled", failedNode,
                           pose.a[failedNode - 1] - currentJoints.a[failedNode - 1], seenStop)) break;
        osDelay(10);
        activeTime += 10;
    }
    armMotionActive = false;
    jointSpeed = previousSpeed;
    return false;
}

bool DummyRobot::RunHand(const char* command)
{
    if (!EnsureHomed() || !WaitReady() || !hand2->isEnabled()) return false;
    float percent = 0;
    const bool opening = strcmp(command, "!HD_O") == 0;
    const bool closing = strcmp(command, "!HD_C") == 0;
    const bool positionMode = strncmp(command, "!HD_POS ", 8) == 0;
    if (!opening && !closing && !positionMode) return false;
    if (positionMode && (sscanf(command + 8, "%f", &percent) != 1 ||
        !std::isfinite(percent) || percent < 0 || percent > 100)) return false;
    uint32_t seenStop = stopEpoch;
    uint32_t sequence = hand2->angleSequence;
    uint32_t activeTime = 0;
    bool send = true;
    uint32_t settlingTime = 0;
    for (;;) {
        if (robotState == STATE_EMERGENCY_STOP || seenStop != stopEpoch) {
            while (robotState != STATE_NORMAL) osDelay(5);
            seenStop = stopEpoch;
            // 急停期间不计入动作超时；恢复后从零开始统计本次实际执行时间。
            activeTime = 0;
            sequence = hand2->angleSequence;
            send = true;
        }
        if (send) {
            settlingTime = 0;
            if (positionMode) hand2->SetPercentToAngle(percent);
            else hand2->SetAngleWithCurrentLimit(opening ? -1 : 1);
            send = false;
        }
        hand2->UpdateAngle();
        osDelay(20);
        activeTime += 20;
        settlingTime += 20;
        // 等待期间可能收到 STOP 和 RESUME，必须先按新代次恢复，再判断旧动作超时。
        if (seenStop != stopEpoch || robotState == STATE_EMERGENCY_STOP) continue;
        bool fresh = hand2->angleSequence != sequence && HAL_GetTick() - hand2->lastAngleTick <= 250;
        float target = hand2->ClosedAngle + percent / 100 * (hand2->OpenedAngle - hand2->ClosedAngle);
        bool arrived = positionMode ? (fabsf(hand2->angle - target) <= 1 && hand2->state == CtrlStepMotor::FINISH) :
            hand2->isStalled;
        // 电流模式的 FINISH 只表示电流爬升完成，不能当成机械动作已经结束。
        if (fresh && arrived && settlingTime >= 350 && seenStop == stopEpoch && robotState != STATE_EMERGENCY_STOP) {
            // 动作结束切换到当前位置保持，避免持续堵转电流。
            hand2->SetAngleWithVelocityLimit(hand2->angle, 1);
            return true;
        }
        if (activeTime >= 10000 || !IsEnabled()) {
            const char* reason = !IsEnabled() ? "hand_disabled" : !fresh ? "hand_feedback_timeout" :
                positionMode ? "hand_not_arrived" : "hand_contact_not_reported";
            if (TryMotionFault(reason, 7, positionMode ? target - hand2->angle : 0, seenStop)) return false;
        }
    }
}

uint32_t DummyRobot::CommandHandler::Push(const std::string& command, uint8_t channel, uint32_t request)
{
    if (context->robotState != STATE_NORMAL) return 0xfe;
    if (command != "!START" && !context->IsEnabled()) return 0xfd;
    if (command.empty() || command.size() >= sizeof(CommandMessage::text)) return 0xfb;
    CommandMessage message{};
    std::memcpy(message.text, command.data(), command.size());
    message.channel = channel;
    message.request = request;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (context->maintenanceActive || context->robotState != STATE_NORMAL) {
        bool busy = context->maintenanceActive;
        __set_PRIMASK(mask);
        return busy ? 0xfc : 0xfe;
    }
    ++context->scheduledActions;
    auto status = osMessageQueuePut(commandFifo, &message, 0, 0);
    if (status != osOK) --context->scheduledActions;
    __set_PRIMASK(mask);
    if (status != osOK) return 0xff;
    return GetSpace();
}
std::string DummyRobot::CommandHandler::Pop(uint32_t timeout)
{
    CommandMessage message{};
    if (osMessageQueueGet(commandFifo, &message, nullptr, timeout) != osOK) return {};
    activeChannel = message.channel;
    activeRequest = message.request;
    message.text[sizeof(message.text) - 1] = 0;
    return message.text;
}
uint32_t DummyRobot::CommandHandler::GetSpace() { return osMessageQueueGetSpace(commandFifo); }
void DummyRobot::CommandHandler::ClearFifo()
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint32_t removed = 16 - GetSpace();
    osMessageQueueReset(commandFifo);
    context->scheduledActions = context->scheduledActions >= removed ? context->scheduledActions - removed : 0;
    __set_PRIMASK(mask);
}
uint32_t DummyRobot::CommandHandler::ParseCommand(const std::string& command)
{
    if (command.empty()) return GetSpace();
    for (;;) {
        uint32_t mask = __get_PRIMASK();
        __disable_irq();
        bool ready = context->robotState == STATE_NORMAL;
        if (ready) context->taskActive = true;
        __set_PRIMASK(mask);
        if (ready) break;
        osDelay(5);
    }
    context->NotifyWorkStart();
    context->motionFault = {};
    TransactionSink executionOutput(Output(activeChannel), activeRequest);
    Respond(executionOutput, "Task Executing");
    bool ok = false;
    const char* tag = "Move";
    if (command == "!START") { tag = "Started"; ok = context->Startup(); }
    else if (command == "!HOME") { tag = "Home"; ok = context->Homing(); }
    else if (command == "!RESET") { tag = "Reset"; ok = context->Resting(); }
    else if (command == "!CLOSE") { tag = "Close"; ok = context->Shutdown(); }
    else if (command == "!CALIBRATION") { tag = "Calibration"; ok = context->CalibrateHomeOffset(); }
    else if (command == "!HD_ZERO") { tag = "HD_ZERO"; ok = context->CalibrateHand(); }
    else if (command == "!HD_EN") { tag = "HD_EN"; ok = context->SetHandEnable(true); }
    else if (command == "!HD_DIS") { tag = "HD_DIS"; ok = context->SetHandEnable(false); }
    else if (command.compare(0, 4, "!HD_") == 0) {
        tag = command.compare(0, 7, "!HD_POS") == 0 ? "HD_POS" : (command == "!HD_O" ? "HD_O" : "HD_C");
        ok = context->RunHand(command.c_str());
    } else {
        float values[6] = {}, speed = context->GetJointSpeed();
        char extra;
        int count = sscanf(command.c_str() + 1, "%f,%f,%f,%f,%f,%f,%f%c", values, values + 1,
                           values + 2, values + 3, values + 4, values + 5, &speed, &extra);
        bool valid = count == 6 || count == 7;
        for (float value : values) if (!std::isfinite(value)) valid = false;
        if (valid && std::isfinite(speed) && speed > 0 && speed <= 100 && context->EnsureHomed() && context->WaitReady()) {
            context->SetJointSpeed(speed);
            bool targetOk = command[0] == '@' ? context->MoveL(values[0], values[1], values[2], values[3], values[4], values[5]) :
                context->MoveJ(values[0], values[1], values[2], values[3], values[4], values[5]);
            if (targetOk) {
                auto target = context->targetJoints;
                ok = context->RunPose(target, speed);
            }
        }
    }
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    context->taskActive = false;
    --context->scheduledActions;
    __set_PRIMASK(mask);
    context->NotifyWorkDone();
    TransactionSink output(Output(activeChannel), activeRequest);
    if (ok && command == "!HD_ZERO")
        Respond(output, "HD_ZERO OK MIN:%.2f MAX:%.2f STATE:%s", context->hand2->ClosedAngle,
                context->hand2->OpenedAngle, context->StateName());
    else if (ok) Respond(output, "%s OK STATE:%s", tag, context->StateName());
    else {
        const auto& fault = context->motionFault;
        Respond(output, "%s Error: task_failed state=%s reason=%s node=%lu age_ms=%lu error_deg=%.2f finished=%lu stalled=%lu",
                tag, context->StateName(), fault.reason, fault.node, fault.feedbackAge,
                fault.error, fault.finished, fault.stalled);
    }
    return GetSpace();
}

void DummyRobot::SetJointSpeed(float value)
{
    if (std::isfinite(value) && value > 0 && value <= 100) jointSpeed = value;
}
void DummyRobot::SetJointAcceleration(float value)
{
    if (!std::isfinite(value) || value <= 0 || value > 100) return;
    for (int n = 1; n <= 6; ++n) motorJ[n]->SetAcceleration(value / 100 * DEFAULT_JOINT_ACCELERATION_BASES.a[n - 1]);
}
void DummyRobot::SetCommandMode(uint32_t mode)
{
    if (mode < 1 || mode > 4 || taskActive) return;
    commandMode = static_cast<CommandMode>(mode);
    // 所有模式使用同一 FIFO 完成屏障，保证暂停和恢复不会覆盖尚未完成的目标。
    SetJointAcceleration(mode == COMMAND_CONTINUES_TRAJECTORY ? DEFAULT_JOINT_ACCELERATION_HIGH : DEFAULT_JOINT_ACCELERATION_LOW);
}
void DummyRobot::PollJointAngle()
{
    // 定向轮询避免六个子板同时应答广播，低优先级节点因仲裁丢失反馈。
    motorJ[feedbackNode]->UpdateAngle();
    feedbackNode = feedbackNode == 6 ? 1 : feedbackNode + 1;
}
void DummyRobot::UpdateJointAngles()
{
    for (int n = 1; n <= 6; ++n) motorJ[n]->UpdateAngle();
}
void DummyRobot::UpdateJointAnglesCallback()
{
    uint8_t flags = 0;
    for (int n = 1; n <= 6; ++n) {
        currentJoints.a[n - 1] = motorJ[n]->angle + initPose.a[n - 1];
        if (motorJ[n]->state == CtrlStepMotor::FINISH) flags |= 1 << n;
    }
    jointsStateFlag = flags;
}
void DummyRobot::UpdateJointPose6D()
{
    dof6Solver->SolveFK(currentJoints, currentPose6D);
    currentPose6D.X *= 1000;
    currentPose6D.Y *= 1000;
    currentPose6D.Z *= 1000;
}
bool DummyRobot::IsMoving() { return armMotionActive; }
bool DummyRobot::IsEnabled() { return isEnabled; }
void DummyRobot::Reboot() { if (robotState == STATE_NORMAL && !taskActive) { motorJ[ALL]->Reboot(); osDelay(500); HAL_NVIC_SystemReset(); } }
bool DummyRobot::SaveNodeOffset(uint8_t node)
{
    if (!WaitReady()) return false;
    bool sent = motorJ[node]->ApplyPositionAsHome();
    if (!sent && robotState == STATE_EMERGENCY_STOP) {
        if (!WaitReady()) return false;
        sent = motorJ[node]->ApplyPositionAsHome();
    }
    if (!sent) return false;
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (!WaitReady()) return false;
        motorJ[node]->QueryConfigSaved();
        for (int wait = 0; wait < 10 && !motorJ[node]->configQueryDone; ++wait) osDelay(5);
        if (motorJ[node]->configQueryDone && motorJ[node]->configSaved) return true;
        osDelay(10);
    }
    return false;
}
bool DummyRobot::CalibrateHomeOffset()
{
    // 沿用人工对齐到 HOME 姿态后启动标定的流程；所有运动均进入可恢复状态机。
    if (!IsEnabled()) return false;
    isHomed = false;
    for (int n = 1; n <= 6; ++n) if (!SaveNodeOffset(n)) { commandHandler.EmergencyStop(); return false; }
    initPose = DOF6Kinematic::Joint6D_t(0, 0, 90, 0, 0, 0);
    UpdateJointAngles();
    osDelay(30);
    if (!Resting()) return false;
    for (int n = 1; n <= 6; ++n) if (!SaveNodeOffset(n)) { commandHandler.EmergencyStop(); return false; }
    initPose = REST_POSE;
    UpdateJointAngles();
    osDelay(30);
    return true;
}
bool DummyRobot::CalibrateHand()
{
    // 用可抢占的电流动作确认两端限位；结束保持使能，不调用旧的阻塞失能函数。
    if (!RunHand("!HD_O") || !RunHand("!HD_C") || !SaveNodeOffset(7)) return false;
    hand2->UpdateAngle();
    osDelay(30);
    hand2->ClosedAngle = hand2->angle;
    if (!RunHand("!HD_O")) return false;
    hand2->OpenedAngle = hand2->angle;
    if (fabsf(hand2->OpenedAngle - hand2->ClosedAngle) < 1) {
        commandHandler.EmergencyStop();
        return false;
    }
    return RunHand("!HD_C");
}
bool DummyRobot::CheckNodeOnline(uint8_t node)
{
    if (node < 1 || node > 7) return false;
    motorJ[node]->QueryOnline();
    osDelay(80);
    return motorJ[node]->isOnline;
}
bool DummyRobot::CheckAllNodesOnline()
{
    for (int n = 1; n <= 7; ++n) motorJ[n]->QueryOnline();
    osDelay(100);
    allNodesOnline = true;
    for (int n = 1; n <= 7; ++n) if (!motorJ[n]->isOnline) allNodesOnline = false;
    return allNodesOnline;
}
void DummyRobot::OnlineCheckTick(uint32_t) { /* 查询由通信工作线程负责，RGB 线程不等待 CAN。 */ }
void DummyRobot::SetRGBEnable(bool enabled) { isRGBEnabled = enabled; }
bool DummyRobot::GetRGBEnabled() { return isRGBEnabled; }
void DummyRobot::SetRGBMode(uint32_t mode) { if (mode <= 11) { rgbMode = mode; SaveRGBModeToEEPROM(); } }
uint32_t DummyRobot::GetRGBMode() { return rgbMode; }
void DummyRobot::LoadRGBModeFromEEPROM() { uint8_t mode = eeprom.read(EEPROM_ADDR_RGB_MODE); rgbMode = mode <= 11 ? mode : 11; }
void DummyRobot::SaveRGBModeToEEPROM() { eeprom.write(EEPROM_ADDR_RGB_MODE, static_cast<uint8_t>(rgbMode)); }
void DummyRobot::SetRGBAutoSubState(uint8_t state)
{
    // 动画计步只由 RGB 线程读写，其他线程仅发布状态。
    rgb.autoSubState = static_cast<RGB::AutoSubState>(state);
}
void DummyRobot::NotifyWorkStart() { if (robotState == STATE_NORMAL) SetRGBAutoSubState(RGB::AUTO_WORKING); }
void DummyRobot::NotifyWorkDone()
{
    if (robotState == STATE_NORMAL) SetRGBAutoSubState(scheduledActions ? RGB::AUTO_WORKING : RGB::AUTO_COMPLETE);
}
void DummyRobot::OnHandActionRGBHook(bool) { NotifyWorkStart(); }
void DummyRobot::AutoModeIdleTick(uint32_t delta)
{
    static uint8_t previous = 0;
    uint8_t desired = robotState == STATE_EMERGENCY_STOP ? RGB::AUTO_ESTOP :
        robotState == STATE_PAUSED ? RGB::AUTO_PAUSED : rgb.autoSubState;
    if (desired != previous) {
        previous = desired;
        rgb.autoSubState = static_cast<RGB::AutoSubState>(desired);
        rgb.ResetEffectSteps();
        autoIdleTickMs = 0;
    }
    if (desired == RGB::AUTO_COMPLETE && !taskActive) {
        autoIdleTickMs += delta;
        if (autoIdleTickMs >= AUTO_IDLE_THRESHOLD_MS) SetRGBAutoSubState(RGB::AUTO_STANDBY);
    }
}
void DummyRobot::TuningHelper::SetTuningFlag(uint8_t flag) { tuningFlag = flag; }
void DummyRobot::TuningHelper::Tick(uint32_t) { /* 调试模式也必须通过统一动作队列。 */ }
void DummyRobot::TuningHelper::SetFreqAndAmp(float freq, float amp) { frequency = freq; amplitude = amp; }
