#include "common_inc.h"
#include <deque>
#include <vector>
#include <string>
#include <functional>
#include <stdexcept>
#include <iostream>
#include <algorithm>

// 编译真实主控调度和协议源文件，只替代 RTOS、CAN、EEPROM 与运动学硬件边界。
struct Queue { size_t capacity, size; std::deque<std::vector<uint8_t>> items; };
void* osMessageQueueNew(uint32_t capacity, uint32_t size, const void*) { return new Queue{capacity, size, {}}; }
int osMessageQueuePut(void* handle, const void* data, uint8_t, uint32_t)
{
    auto& queue = *static_cast<Queue*>(handle);
    if (queue.items.size() == queue.capacity) return 1;
    auto* bytes = static_cast<const uint8_t*>(data);
    queue.items.emplace_back(bytes, bytes + queue.size);
    return osOK;
}
int osMessageQueueGet(void* handle, void* data, uint8_t*, uint32_t)
{
    auto& queue = *static_cast<Queue*>(handle);
    if (queue.items.empty()) return 1;
    memcpy(data, queue.items.front().data(), queue.size);
    queue.items.pop_front();
    return osOK;
}
uint32_t osMessageQueueGetSpace(void* handle) { auto& q = *static_cast<Queue*>(handle); return q.capacity - q.items.size(); }
void osMessageQueueReset(void* handle) { static_cast<Queue*>(handle)->items.clear(); }
void* osThreadNew(void (*)(void*), void*, const osThreadAttr_t*) { return reinterpret_cast<void*>(1); }
uint32_t osThreadFlagsSet(void*, uint32_t) { extern DummyRobot dummy; dummy.ApplyEmergencyBrake(); return 0; }
uint32_t osThreadFlagsWait(uint32_t, uint32_t, uint32_t) { return 0; }
static uint32_t tick = 0;
static std::function<void()> delayHook;
void osDelay(uint32_t amount) { tick += amount; if (delayHook) delayHook(); }
uint32_t HAL_GetTick() { return tick; }
static unsigned resets = 0;
void HAL_NVIC_SystemReset() { ++resets; }
TIM_HandleTypeDef htim2;

struct Output : StreamSink {
    std::string text;
    int process_bytes(const uint8_t* bytes, size_t size, size_t*) override { text.append(reinterpret_cast<const char*>(bytes), size); return 0; }
    size_t get_free_space() override { return 99999; }
};
Output usb, uart4, uart5;
StreamSink* usbStreamOutputPtr = &usb;
StreamSink* uart4StreamOutputPtr = &uart4;
StreamSink* uart5StreamOutputPtr = &uart5;
CAN_HandleTypeDef bus;
DummyRobot dummy(&bus);
RGB rgb;
DOF6Kinematic::DOF6Kinematic(float, float, float, float, float, float) {}
bool DOF6Kinematic::SolveFK(const Joint6D_t&, Pose6D_t&) { return true; }
bool DOF6Kinematic::SolveIK(const Pose6D_t&, const Joint6D_t&, IKSolves_t& result)
{
    for (auto& config : result.config) config = Joint6D_t(0, 0, 90, 0, 0, 0);
    return true;
}
CAN_context* get_can_ctx(CAN_HandleTypeDef* value) { static CAN_context ctx{value}; return &ctx; }
static bool frozen = false;
static int missingParameter = -1;
static bool failSave = false;
static unsigned disableCount = 0;
static unsigned brakeCount = 0;
static bool failEnableSend = false;
static bool stopOnDisableSend = false;
static bool failOffsetSend = false;
static bool currentFinishedWithoutContact = false;
static float handPosition = 0;
static int32_t gains[8][4] = {};
static std::vector<float> j1Targets;
static std::vector<int> angleQueries;
static bool soundSupported = true;
static bool staleSoundAck = false;
static uint8_t simulatedSoundState = 0;
static uint32_t soundEndTick = 0;
static unsigned soundStarts = 0;
static uint8_t soundCapability = 3;
static uint8_t simulatedWave = 0;
static bool zeroSoundOutput = false, dropSoundDiagnostics = false;
static bool simulatedHold = false;
static uint32_t holdReadyTick = 0;
static unsigned holdStarts = 0;
static std::vector<std::pair<int, int>> acceptedFrames;
bool CanSendMessage(CAN_context*, uint8_t* data, CAN_TxHeaderTypeDef* header)
{
    int node = header->StdId >> 7;
    uint8_t command = header->StdId & 127;
    if (command == 1 && failEnableSend) return false;
    if (command == 1 && !data[0] && stopOnDisableSend) {
        stopOnDisableSend = false;
        dummy.RequestEmergencyStop();
    }
    if (!dummy.CanTransmit(command, data)) return false;
    acceptedFrames.emplace_back(node, command);
    if (command == 0x30 && node == 1) {
        if (!soundSupported) return true;
        if (data[0] == 1 || data[0] == 4 || data[0] == 5) {
            ++soundStarts;
            simulatedWave = data[0] == 1 ? 0 : data[0] - 3;
            simulatedSoundState = 1;
            soundEndTick = tick + (data[4] | (uint16_t(data[5]) << 8));
        } else if (data[0] == 0) simulatedSoundState = 3;
        else if (simulatedSoundState == 1 && tick >= soundEndTick) simulatedSoundState = 2;
        if (data[0] == 3 && soundCapability == 3) {
            ++holdStarts;
            if (!simulatedHold) holdReadyTick = tick + 10;
            simulatedHold = true;
        }
        dummy.motorJ[1]->soundResult = 0;
        dummy.motorJ[1]->soundState = simulatedSoundState;
        dummy.motorJ[1]->soundCapability = soundCapability;
        dummy.motorJ[1]->soundHoldActive = simulatedHold;
        dummy.motorJ[1]->soundHoldReady = simulatedHold && tick >= holdReadyTick;
        dummy.motorJ[1]->soundAckToken = staleSoundAck ? data[1] - 1 : data[1];
        if (data[0] == 6 && !dropSoundDiagnostics) {
            dummy.motorJ[1]->soundDiagnosticToken = staleSoundAck ? data[1] - 1 : data[1];
            dummy.motorJ[1]->soundInjectedTicks = soundStarts && !zeroSoundOutput ? 3800 : 0;
            dummy.motorJ[1]->soundInjectedPeak = soundStarts && !zeroSoundOutput ? 50 : 0;
            dummy.motorJ[1]->soundWave = simulatedWave;
        }
    }
    if (command == 4 && (node == 0 || node == 1)) simulatedSoundState = 3;
    if (command == 0x15 && failOffsetSend) return false;
    if (command == 0x15 && node == 7) handPosition = 0;
    if (command == 0x15 && node >= 1 && node <= 7) dummy.motorJ[node]->UpdateAngleCallback(0, true);
    if (command == 1 && !data[0]) ++disableCount;
    if (command == 4) ++brakeCount;
    if (command == 7 && node >= 1 && node <= 7) {
        auto* motor = dummy.motorJ[node];
        float turns;
        memcpy(&turns, data, 4);
        float target = turns / motor->reduction * 360 * (motor->inverseDirection ? -1 : 1);
        if (node == 1) j1Targets.push_back(target);
        if (!frozen) {
            float distance = target - motor->angle;
            float next = motor->angle + std::clamp(distance, -5.0f, 5.0f);
            float raw = next / 360 * motor->reduction * (motor->inverseDirection ? -1 : 1);
            motor->UpdateAngleCallback(raw, fabsf(target - next) < 0.1f, false);
        }
    }
    if (command == 3 && node == 7) {
        float current; memcpy(&current, data, 4);
        if (!currentFinishedWithoutContact) handPosition = current < 0 ? -5.0f : 0;
        dummy.hand2->UpdateAngleCallback(handPosition, true, !currentFinishedWithoutContact);
    }
    if (command == 0x23) {
        angleQueries.push_back(node);
        for (int n = node ? node : 1; n <= (node ? node : 7); ++n) {
            auto* motor = dummy.motorJ[n];
            motor->UpdateAngleCallback(motor->angle / 360 * motor->reduction * (motor->inverseDirection ? -1 : 1), motor->state == CtrlStepMotor::FINISH, motor->isStalled);
            motor->OnlineCallback();
        }
    }
    if (command >= 0x17 && command <= 0x1a && node) memcpy(&gains[node][command - 0x17], data, 4);
    if (command >= 0x27 && command <= 0x2a && node && command != missingParameter)
        dummy.motorJ[node]->DceParamCallback(command, gains[node][command - 0x27]);
    if (command == 0x2b && node) dummy.motorJ[node]->ConfigSavedCallback(!failSave);
    dummy.UpdateJointAnglesCallback();
    return true;
}
bool ProcessAsciiCommand(uint32_t timeout);
static void Require(bool condition, const char* name) { if (!condition) throw std::runtime_error(name); }
static void Send(const std::string& command, Output& output = usb)
{
    OnUsbAsciiCmd(command.c_str(), command.size(), output);
}
static void RunOne() { dummy.commandHandler.ParseCommand(dummy.commandHandler.Pop(0)); }
static void Reset()
{
    delayHook = {};
    dummy.j1TestMode = false;
    dummy.robotState = DummyRobot::STATE_NORMAL;
    dummy.commandHandler.ClearFifo();
    dummy.scheduledActions = 0;
    dummy.taskActive = false;
    dummy.pauseRequested = false;
    dummy.SetEnable(true);
    dummy.hand2->SetEnable(true);
    frozen = false;
    missingParameter = -1;
    failSave = false;
    usb.text.clear(); uart4.text.clear(); uart5.text.clear();
    j1Targets.clear();
}
int main()
{
    try {
        InitAsciiCommands();
        EmergencyStopScanner scanner;
        auto scan = [&](const std::string& text) {
            return scanner.Feed(reinterpret_cast<const uint8_t*>(text.data()), text.size());
        };
        Require(!scan("!ST") && scan("OP\n"), "stop_scanner_handles_split_line");
        Require(!scan("!STOP_EXTRA\n") && !scan("!RGB_CUSTOM !STOP\n"), "stop_scanner_ignores_substrings");
        Require(!scan("T:bad !STOP\n") && !scan("T:2147483648 !STOP\n"), "stop_scanner_rejects_invalid_transaction");
        Require(scan("T:123 !stop\r\n"), "stop_scanner_accepts_wrapped_lowercase");
        uart4.channelType = StreamSink::CHANNEL_TYPE_UART4;
        uart5.channelType = StreamSink::CHANNEL_TYPE_UART5;
        Send("!HOME");
        Require(usb.text.find("not_enabled") != std::string::npos, "disabled_home");
        Reset();
        Send("T:11 &20,0,90,0,0,0,30");
        Send("T:12 &40,0,90,0,0,0,30");
        int phase = 0;
        delayHook = [&] {
            if (phase == 0 && dummy.IsMoving()) { phase = 1; Send("!STOP"); Send("!STOP"); }
            else if (phase == 1) {
                Require(dummy.IsEnabled() && dummy.commandHandler.GetSpace() == 15, "stop_keeps_queue_enable");
                Require(dummy.hasPendingResumeTarget, "stop_keeps_current_target");
                Send("!PAUSE");
                Require(dummy.robotState == DummyRobot::STATE_EMERGENCY_STOP, "pause_cannot_downgrade_stop");
                Send("!RGB_OFF");
                Require(usb.text.find("RGB_OFF Error: state=EMERGENCY_STOP") != std::string::npos, "stop_rejects_rgb");
                Send("#GETSPEED"); ProcessAsciiCommand(0);
                Require(usb.text.find("GetSpeed OK") != std::string::npos, "stop_allows_query");
                phase = 2; Send("!RESUME");
            }
        };
        RunOne();
        delayHook = {};
        Require(phase == 2 && dummy.IsHomed(), "auto_home_resumes_after_stop");
        Require(usb.text.find("Home OK") == std::string::npos, "automatic_home_is_silent");
        Require(usb.text.find("R:11 Move OK") != std::string::npos, "request_id_preserved");
        RunOne();
        Require(fabsf(dummy.currentJoints.a[0] - 40) < 0.5f, "remaining_queue_runs");

        Reset();
        Send("&50,0,90,0,0,0,30"); Send("&60,0,90,0,0,0,30");
        bool paused = false;
        bool pausePendingObserved = false;
        delayHook = [&] {
            if (!paused && dummy.IsMoving()) {
                paused = true;
                Send("!PAUSE");
                pausePendingObserved = dummy.robotState == DummyRobot::STATE_NORMAL &&
                    dummy.IsPausePending() && std::string(dummy.StateName()) == "PAUSE_PENDING";
            }
        };
        RunOne(); delayHook = {};
        Require(paused && pausePendingObserved && dummy.robotState == DummyRobot::STATE_PAUSED,
                "soft_pause_state_after_action");
        Require(fabsf(dummy.currentJoints.a[0] - 50) < 0.5f && dummy.commandHandler.GetSpace() == 15, "soft_pause_finishes_only_current");
        Send("!STOP"); Require(dummy.robotState == DummyRobot::STATE_EMERGENCY_STOP, "pause_escalates");
        Send("!RESUME"); RunOne();
        Require(fabsf(dummy.currentJoints.a[0] - 60) < 0.5f, "resume_next_command");

        Reset();
        handPosition = -5.0f;
        dummy.hand2->UpdateAngleCallback(handPosition, true, true);
        unsigned before = disableCount; bool intercepted = false;
        Send("!CLOSE");
        delayHook = [&] {
            if (!intercepted && dummy.IsMoving()) { intercepted = true; Send("!STOP"); }
            else if (intercepted && dummy.robotState == DummyRobot::STATE_EMERGENCY_STOP) {
                Require(disableCount == before && dummy.IsEnabled(), "close_stop_never_disables");
                Send("!RESUME");
            }
        };
        RunOne(); delayHook = {};
        Require(intercepted && handPosition >= 0 && !dummy.IsEnabled() && disableCount > before,
                "close_closes_hand_before_disable");
        before = brakeCount;
        Send("!STOP"); dummy.ApplyEmergencyBrake();
        Require(!dummy.IsEnabled() && !dummy.hand2->isEnabled() && brakeCount == before,
                "stop_does_not_reenable_closed_robot");
        Send("!RESUME");

        failEnableSend = true;
        Require(!dummy.SetEnable(true) && !dummy.IsEnabled() && !dummy.hand2->isEnabled(), "failed_enable_does_not_change_flags");
        failEnableSend = false;
        Reset();
        Require(dummy.Resting(), "prepare_disable_race");
        before = disableCount;
        stopOnDisableSend = true;
        Require(!dummy.Disable() && dummy.IsEnabled() && dummy.hand2->isEnabled() && disableCount == before,
                "stop_between_disable_check_and_send_keeps_enable");
        Send("!RESUME");

        Reset(); before = disableCount;
        tick += 300;
        Require(!dummy.Disable() && dummy.IsEnabled() && disableCount == before, "disable_requires_fresh_rest_feedback");
        Send("!HD_DIS"); RunOne();
        Require(dummy.IsEnabled() && !dummy.hand2->isEnabled(), "hand_disable_command");
        Send("!HD_EN"); RunOne();
        Require(dummy.IsEnabled() && dummy.hand2->isEnabled(), "hand_enable_command");
        Send("#SETPID 7 101 202 303 404"); ProcessAsciiCommand(0);
        Require(usb.text.find("SetPid OK") != std::string::npos && gains[7][0] == 101 && gains[7][1] == 404 && gains[7][2] == 202 && gains[7][3] == 303, "pidv_all_order_and_hand");
        Send("#SETPID 1 kv 99"); ProcessAsciiCommand(0);
        Require(gains[1][1] == 99, "pidv_single");
        missingParameter = 0x29;
        usb.text.clear(); Send("#GETPID 7"); ProcessAsciiCommand(0);
        Require(usb.text.find("J7  NL:") != std::string::npos && usb.text.find("KI:") == std::string::npos, "missing_pid_not_reported_from_cache");
        missingParameter = -1;
        unsigned priorResets = resets;
        Send("#OFFSET_J all"); ProcessAsciiCommand(0);
        Require(resets == priorResets + 1, "offset_all_reboots_after_save");
        failSave = true;
        Send("#OFFSET_J all"); ProcessAsciiCommand(0);
        Require(resets == priorResets + 1, "offset_save_failure_never_reboots");
        failSave = false;
        failOffsetSend = true;
        Send("#OFFSET_J all"); ProcessAsciiCommand(0);
        Require(resets == priorResets + 1 && usb.text.find("send_failed") != std::string::npos,
                "offset_send_failure_never_reboots");
        failOffsetSend = false;

        bool stoppedDuringSave = false;
        delayHook = [&] {
            if (!stoppedDuringSave) { stoppedDuringSave = true; Send("!STOP"); }
        };
        Send("#OFFSET_J all"); ProcessAsciiCommand(0); delayHook = {};
        Require(stoppedDuringSave && resets == priorResets + 1 && dummy.IsEnabled(),
                "stop_during_offset_prevents_reboot");
        Send("!RESUME");

        Reset();
        dummy.InvalidateHome();
        Send("&50,0,90,0,0,0,30");
        bool pausedDuringHome = false;
        delayHook = [&] {
            if (!pausedDuringHome && dummy.IsMoving()) { pausedDuringHome = true; Send("!PAUSE"); }
        };
        RunOne(); delayHook = {};
        Require(pausedDuringHome && fabsf(dummy.currentJoints.a[0] - 50) < 0.5f &&
                dummy.robotState == DummyRobot::STATE_PAUSED, "pause_finishes_action_including_auto_home");
        Send("!RESUME");

        Reset();
        Require(dummy.Homing(), "prepare_interrupted_target");
        Send("&70,0,90,0,0,0,30");
        bool stoppedAtTarget = false;
        delayHook = [&] {
            if (!stoppedAtTarget && dummy.IsMoving()) { stoppedAtTarget = true; Send("!STOP"); }
            else if (dummy.robotState == DummyRobot::STATE_EMERGENCY_STOP) Send("!RESUME");
        };
        RunOne(); delayHook = {};
        Require(stoppedAtTarget && fabsf(dummy.currentJoints.a[0] - 70) < 0.5f,
                "stop_resumes_actual_user_target");

        Reset();
        currentFinishedWithoutContact = false;
        Send("!HD_O");
        bool stoppedHand = false;
        delayHook = [&] {
            if (!dummy.IsMoving() && dummy.hand2->isStalled && !stoppedHand) {
                stoppedHand = true; Send("!STOP");
            } else if (dummy.robotState == DummyRobot::STATE_EMERGENCY_STOP) Send("!RESUME");
        };
        RunOne(); delayHook = {};
        Require(stoppedHand && dummy.IsEnabled() && usb.text.find("HD_O OK") != std::string::npos,
                "hand_action_resumes_without_disabling");

        Reset();
        Send("!HD_ZERO");
        before = disableCount;
        RunOne();
        Require(dummy.IsEnabled() && dummy.hand2->isEnabled() && disableCount == before &&
                usb.text.find("HD_ZERO OK MIN:") != std::string::npos, "hand_calibration_keeps_enabled");

        Reset();
        currentFinishedWithoutContact = true;
        Send("!HD_O");
        const uint32_t handResumeBaseline = tick;
        bool handStoppedNearTimeout = false;
        delayHook = [&] {
            if (!handStoppedNearTimeout && tick - handResumeBaseline >= 9800) {
                handStoppedNearTimeout = true;
                Send("!STOP");
                currentFinishedWithoutContact = false;
                Send("!RESUME");
            }
        };
        RunOne();
        delayHook = {};
        Require(handStoppedNearTimeout && dummy.robotState == DummyRobot::STATE_NORMAL &&
                usb.text.find("HD_O OK") != std::string::npos,
                "hand_resume_resets_timeout");

        Reset();
        Send("!CALIBRATION"); RunOne();
        Require(dummy.IsEnabled() && usb.text.find("Calibration OK") != std::string::npos,
                "arm_calibration_uses_resumable_motion");
        for (int n = 0; n < 6; ++n)
            Require(fabsf(dummy.currentJoints.a[n] - dummy.REST_POSE.a[n]) < 0.5f, "calibration_finishes_at_rest");

        Reset();
        currentFinishedWithoutContact = true;
        Send("!HD_C"); RunOne();
        Require(dummy.robotState == DummyRobot::STATE_EMERGENCY_STOP &&
                usb.text.find("HD_C OK") == std::string::npos, "current_settled_is_not_grip_complete");
        Require(usb.text.find("reason=hand_contact_not_reported node=7") != std::string::npos,
                "hand_timeout_reports_missing_contact");
        Send("#STATUS"); ProcessAsciiCommand(0);
        Require(usb.text.find("FAULT:hand_contact_not_reported NODE:7") != std::string::npos,
                "fault_snapshot_query_allowed_during_stop");
        currentFinishedWithoutContact = false;

        Reset();
        currentFinishedWithoutContact = true;
        bool stoppedAtHandDeadline = false;
        uint32_t handDeadlineStart = 0;
        delayHook = [&] {
            if (!dummy.IsMoving() && dummy.hand2->state == CtrlStepMotor::FINISH) {
                if (!handDeadlineStart) handDeadlineStart = tick - 20;
                if (!stoppedAtHandDeadline && tick - handDeadlineStart >= 10000) {
                    stoppedAtHandDeadline = true;
                    Send("!STOP");
                    currentFinishedWithoutContact = false;
                    Send("!RESUME");
                }
            }
        };
        Send("!HD_O"); RunOne();
        delayHook = {};
        currentFinishedWithoutContact = false;
        Require(stoppedAtHandDeadline && dummy.robotState == DummyRobot::STATE_NORMAL &&
                usb.text.find("HD_O OK") != std::string::npos,
                "stop_on_hand_deadline_does_not_fail_resumed_task");

        Reset();
        angleQueries.clear();
        for (int n = 0; n < 12; ++n) dummy.PollJointAngle();
        for (int n = 0; n < 12; ++n)
            Require(angleQueries[n] == n % 6 + 1, "idle_feedback_round_robin_no_broadcast");
        angleQueries.clear();
        dummy.UpdateJointAngles();
        Require(angleQueries == std::vector<int>({1, 2, 3, 4, 5, 6}), "joint_query_is_addressed");
        Require(!dummy.TryMotionFault("stale_timeout", 7, 0, dummy.stopEpoch - 1) &&
                dummy.robotState == DummyRobot::STATE_NORMAL, "stale_epoch_cannot_estop");

        Reset();
        auto sequence = dummy.motorJ[1]->angleSequence;
        dummy.motorJ[1]->UpdateAngleCallback(NAN, true);
        Require(dummy.motorJ[1]->angleSequence == sequence, "nonfinite_feedback_rejected");
        for (int mode = 0; mode <= 11; ++mode) rgb.Run(static_cast<RGB::Rgb_style_t>(mode));
        dummy.NotifyWorkDone();
        for (int i = 0; i < 170; ++i) dummy.AutoModeIdleTick(30);
        Require(rgb.autoSubState == RGB::AUTO_STANDBY, "complete_light_returns_to_standby");

        Reset(); frozen = true; before = disableCount;
        Send("!CLOSE"); RunOne();
        Require(disableCount == before && dummy.IsEnabled() && dummy.robotState == DummyRobot::STATE_EMERGENCY_STOP, "close_feedback_timeout_never_disables");

        Reset();
        Send("#GETSPEED"); ProcessAsciiCommand(0);
        Send("!RGB_MODE 9"); ProcessAsciiCommand(0);
        Require(j1Targets.empty(), "query_and_rgb_do_not_home");
        Send("&nan,0,90,0,0,0,30");
        Send("!HD_POS 101");
        Require(dummy.commandHandler.GetSpace() == 16 && j1Targets.empty(), "invalid_action_never_homes");

        Reset();
        const std::string usbPart = "#GET";
        const std::string uartLine = "#GETSPEED\n";
        const std::string usbTail = "SPEED\n";
        ASCII_protocol_parse_stream(reinterpret_cast<const uint8_t*>(usbPart.data()), usbPart.size(), usb);
        ASCII_protocol_parse_stream(reinterpret_cast<const uint8_t*>(uartLine.data()), uartLine.size(), uart4);
        ASCII_protocol_parse_stream(reinterpret_cast<const uint8_t*>(usbTail.data()), usbTail.size(), usb);
        ProcessAsciiCommand(0); ProcessAsciiCommand(0);
        Require(usb.text.find("GetSpeed OK") != std::string::npos && uart4.text.find("GetSpeed OK") != std::string::npos, "split_channels_are_independent");
        // 实验入口验证：无隐式归位，未更新子板拒绝，短音成功及 STOP 中断。
        dummy.InitSysCmdThread();
        Reset();
        dummy.InvalidateHome();
        Send("!J1_SOUND 1000 200 50"); ProcessAsciiCommand(0);
        Require(usb.text.find("require_enabled_and_homed") != std::string::npos &&
                j1Targets.empty(), "sound_never_implicitly_homes");
        Send("!HOME"); RunOne();
        j1Targets.clear(); usb.text.clear();
        Send("!J1_SOUND 1000 501 50"); ProcessAsciiCommand(0);
        Require(usb.text.find("bad_args") != std::string::npos, "sound_duration_limit");
        soundSupported = false;
        Send("!J1_SOUND 1000 200 50"); ProcessAsciiCommand(0);
        Require(usb.text.find("no_experiment_ack") != std::string::npos && !soundStarts,
                "sound_requires_experiment_board");
        soundSupported = true;
        staleSoundAck = true;
        usb.text.clear();
        Send("#J1_SOUND"); ProcessAsciiCommand(0);
        Require(usb.text.find("no_experiment_ack") != std::string::npos, "sound_rejects_stale_ack");
        staleSoundAck = false;
        usb.text.clear();
        Send("!J1_SOUND 1000 200 50"); ProcessAsciiCommand(0);
        Require(usb.text.find("J1Sound OK SOUND:2") != std::string::npos && j1Targets.empty(),
                "sound_completes_without_position_command");
        usb.text.clear();
        bool soundStopped = false;
        delayHook = [&] {
            if (!soundStopped && simulatedSoundState == 1) {
                soundStopped = true;
                Send("!STOP");
                Require(simulatedSoundState == 3 && dummy.IsEnabled(), "stop_cancels_sound_keeps_enable");
            }
        };
        Send("!J1_SOUND 1000 500 50"); ProcessAsciiCommand(0);
        delayHook = {};
        Require(soundStopped && usb.text.find("J1Sound Error: cancelled") != std::string::npos,
                "sound_stop_is_not_success");
        const auto startsBefore = soundStarts;
        Send("!J1_SOUND 1000 200 50");
        Require(soundStarts == startsBefore, "estop_rejects_new_sound");
        Send("#J1_SOUND"); ProcessAsciiCommand(0);
        Require(usb.text.find("EXP:3 SOUND:3 STATE:EMERGENCY_STOP") != std::string::npos,
                "sound_query_allowed_during_stop");
        Send("!RESUME");
        Require(soundStarts == startsBefore, "resume_does_not_replay_sound");

        // 单节点实验不依赖整臂使能或 HOME，始终使用本地当前位置保持。
        Reset();
        Send("!CLOSE"); RunOne();
        Require(!dummy.IsEnabled() && !dummy.IsHomed(), "single_node_starts_with_arm_disabled");
        usb.text.clear(); j1Targets.clear(); acceptedFrames.clear();
        soundCapability = 2;
        Send("!J1_TEST_START"); ProcessAsciiCommand(0);
        Require(usb.text.find("require_exp3_board") != std::string::npos &&
                !dummy.j1TestMode && !holdStarts, "single_node_rejects_old_board_without_enable");
        soundCapability = 3;
        usb.text.clear();
        Send("!J1_TEST_START"); ProcessAsciiCommand(0);
        Require(usb.text.find("J1TestStart OK MODE:J1_ONLY HOLD:1 READY:1") != std::string::npos &&
                dummy.j1TestMode && !dummy.IsEnabled() && !dummy.IsHomed() && simulatedHold,
                "single_node_hold_does_not_forge_arm_state");
        Send("!J1_TEST_START"); ProcessAsciiCommand(0);
        Require(j1Targets.empty(), "repeated_prepare_has_no_position_command");
        for (const auto& frame : acceptedFrames)
            Require(frame.first == 1 && frame.second == 0x30, "prepare_only_addresses_j1_experiment");
        const char* rejected[] = {"!START", "!HOME", "!CLOSE", "!HD_O", "&1,0,90,0,0,0,5", "#SETPID 1 P 123"};
        for (const auto* command : rejected) Send(command);
        Require(dummy.commandHandler.GetSpace() == 16 &&
                usb.text.find("j1_test_only") != std::string::npos, "single_node_rejects_arm_actions_and_parameters");
        float target = 100;
        uint32_t enable = 1;
        Require(!dummy.CanTransmit(0x07, reinterpret_cast<uint8_t*>(&target)) &&
                !dummy.CanTransmit(0x01, reinterpret_cast<uint8_t*>(&enable)), "single_node_blocks_lower_level_motion");
        Send("#STATUS"); ProcessAsciiCommand(0);
        Require(usb.text.find("J1_TEST_MODE:1") != std::string::npos &&
                usb.text.find("ENABLED:0 HOMED:0 ACTIVE:0 QUEUE:0") != std::string::npos, "single_node_status_is_explicit");
        usb.text.clear(); acceptedFrames.clear();
        Send("!J1_SOUND 1000 200 50"); ProcessAsciiCommand(0);
        Require(usb.text.find("J1Sound OK SOUND:2") != std::string::npos &&
                j1Targets.empty(), "single_node_sound_without_arm_home");
        for (const auto* wave : {"Q_SINE", "Q_SQUARE"}) {
            usb.text.clear();
            const std::string command = std::string("!J1_SOUND 1000 200 50 ") + wave;
            Send(command.c_str()); ProcessAsciiCommand(0);
            Require(usb.text.find(std::string("WAVE:") + wave) != std::string::npos &&
                    usb.text.find("J1Sound OK SOUND:2") != std::string::npos &&
                    usb.text.find("CMD_TICKS:3800 PEAK_CMD_MA:50") != std::string::npos,
                    "q_modes_execute_and_report_output_commands");
        }
        const auto beforeBadWave = soundStarts;
        Send("!J1_SOUND 1000 200 50 UNKNOWN"); ProcessAsciiCommand(0);
        Require(soundStarts == beforeBadWave, "invalid_wave_cannot_start_output");
        usb.text.clear(); zeroSoundOutput = true;
        Send("!J1_SOUND 1000 200 50 Q_SINE"); ProcessAsciiCommand(0);
        Require(usb.text.find("Error: incomplete") != std::string::npos &&
                usb.text.find("J1Sound OK SOUND:2") == std::string::npos,
                "zero_output_must_not_report_success");
        zeroSoundOutput = false; dropSoundDiagnostics = true; usb.text.clear();
        Send("#J1_SOUND"); ProcessAsciiCommand(0);
        Require(usb.text.find("no_experiment_ack") != std::string::npos,
                "normal_ack_cannot_replace_diagnostic_reply");
        dropSoundDiagnostics = false;
        bool singleStopped = false;
        delayHook = [&] {
            if (!singleStopped && simulatedSoundState == 1) {
                singleStopped = true;
                Send("!STOP");
                Require(simulatedSoundState == 3 && simulatedHold && dummy.j1TestMode,
                        "single_stop_cancels_excitation_and_keeps_hold");
            }
        };
        Send("!J1_SOUND 1000 500 50 Q_SQUARE"); ProcessAsciiCommand(0);
        delayHook = {};
        Require(singleStopped && usb.text.find("J1Sound Error: cancelled") != std::string::npos,
                "single_node_stop_works_with_arm_disabled");
        for (const auto& frame : acceptedFrames)
            Require(frame.first == 1 && frame.second == 0x30, "single_stop_never_broadcasts_or_changes_mode");
        Send("#J1_SOUND"); ProcessAsciiCommand(0);
        Require(usb.text.find("EXP:3 SOUND:3 STATE:EMERGENCY_STOP MODE:J1_ONLY HOLD:1 READY:1") != std::string::npos,
                "single_node_estop_query_reports_hold");
        const auto singleStarts = soundStarts;
        Send("!RESUME");
        Require(soundStarts == singleStarts && dummy.j1TestMode && simulatedHold,
                "single_resume_keeps_isolation_without_replay");
        Send("!PAUSE");
        Send("!J1_SOUND 1000 200 50");
        Require(soundStarts == singleStarts && dummy.robotState == DummyRobot::STATE_PAUSED,
                "single_node_pause_blocks_new_sound");
        Send("!STOP");
        Require(dummy.robotState == DummyRobot::STATE_EMERGENCY_STOP, "single_pause_upgrades_to_stop");
        Send("!RESUME");

        // 准备期间的 STOP 也必须取消；锁存保留，不能转入整臂流程。
        dummy.j1TestMode = false; simulatedHold = false;
        bool prepareStopped = false;
        delayHook = [&] {
            if (!prepareStopped && dummy.j1TestMode) { prepareStopped = true; Send("!STOP"); }
        };
        usb.text.clear();
        Send("!J1_TEST_START"); ProcessAsciiCommand(0);
        delayHook = {};
        Require(prepareStopped && dummy.j1TestMode && simulatedHold &&
                dummy.robotState == DummyRobot::STATE_EMERGENCY_STOP &&
                usb.text.find("J1TestStart OK") == std::string::npos,
                "stop_during_prepare_never_reports_success");
        std::cout << "firmware runtime scenarios passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
