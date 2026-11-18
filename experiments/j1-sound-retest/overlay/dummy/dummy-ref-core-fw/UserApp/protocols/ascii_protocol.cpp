#include "common_inc.h"
#include "runtime_failure.hpp"
#include "ascii_processor.hpp"
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cerrno>

extern DummyRobot dummy;
extern RGB rgb;

namespace {
struct Message {
    char text[128];
    uint8_t channel;
    uint32_t request;
};
osMessageQueueId_t maintenanceQueue;

StreamSink& Output(uint8_t channel)
{
    if (channel == StreamSink::CHANNEL_TYPE_USB) return *usbStreamOutputPtr;
    if (channel == StreamSink::CHANNEL_TYPE_UART5) return *uart5StreamOutputPtr;
    return *uart4StreamOutputPtr;
}
bool Is(const char* command, const char* token)
{
    size_t size = strlen(token);
    return strncmp(command, token, size) == 0 && (command[size] == 0 || command[size] == ' ' || command[size] == '\t');
}
const char* Tag(const char* command)
{
    if (strchr("&@>", command[0])) return "Move";
    struct Entry { const char* command; const char* tag; };
    static const Entry entries[] = {
#ifdef J1_SOUND_EXPERIMENT
        {"!J1_SOUND", "J1Sound"}, {"#J1_SOUND", "J1Sound"},
        {"!J1_TEST_START", "J1TestStart"},
#endif
        {"!START", "Started"}, {"!HOME", "Home"}, {"!RESET", "Reset"}, {"!CLOSE", "Close"},
        {"!HD_O", "HD_O"}, {"!HD_C", "HD_C"}, {"!HD_POS", "HD_POS"}, {"!HD_ZERO", "HD_ZERO"},
        {"!HD_EN", "HD_EN"}, {"!HD_DIS", "HD_DIS"}, {"!HD_I", "HD_I"},
        {"!RGB_ON", "RGB_ON"}, {"!RGB_OFF", "RGB_OFF"}, {"!RGB_MODE", "RGB_MODE"}, {"!RGB_CUSTOM", "RGB_CUSTOM"},
        {"#GETJPOS", "GetJPos"}, {"#GETLPOS", "GetLPos"}, {"#GETSPEED", "GetSpeed"},
        {"#STATUS", "Status"}, {"#ONLINE", "Online"}, {"#GET_DCE", "GetDce"},
        {"#GETPID", "GetPid"}, {"GETPID", "GetPid"}, {"#SETPID", "SetPid"}, {"SETPID", "SetPid"},
        {"#SET_DCE_KP", "SetDceKp"}, {"#SET_DCE_KI", "SetDceKi"}, {"#SET_DCE_KD", "SetDceKd"}, {"#SET_DCE_KV", "SetDceKv"},
        {"#OFFSET_J", "OffsetJ"}, {"#CMDMODE", "CmdMode"}, {"#REBOOT", "Reboot"},
        {"#ACC_J", "AccJ"}, {"#SPEED_J", "SpeedJ"}, {"#I_LIMIT_J", "ILimitJ"}, {"!CALIBRATION", "Calibration"},
    };
    for (const auto& entry : entries) if (Is(command, entry.command)) return entry.tag;
    return "Command";
}
bool Query(const char* command)
{
#ifdef J1_SOUND_EXPERIMENT
    if (Is(command, "#J1_SOUND")) return true;
#endif
    return Is(command, "#GETJPOS") || Is(command, "#GETLPOS") || Is(command, "#GETSPEED") ||
        Is(command, "#STATUS") || Is(command, "#ONLINE") || Is(command, "#GET_DCE") ||
        Is(command, "#GETPID") || Is(command, "GETPID");
}
bool Action(const char* command)
{
    return strchr("&@>", command[0]) || Is(command, "!START") || Is(command, "!HOME") ||
        Is(command, "!RESET") || Is(command, "!CLOSE") || Is(command, "!HD_O") ||
        Is(command, "!HD_C") || Is(command, "!HD_POS") || Is(command, "!HD_ZERO") ||
        Is(command, "!HD_EN") || Is(command, "!HD_DIS") || Is(command, "!CALIBRATION");
}
bool Unsigned(const char* token, uint32_t& value)
{
    if (!token[0]) return false;
    for (const char* p = token; *p; ++p) if (*p < '0' || *p > '9') return false;
    errno = 0;
    char* end;
    unsigned long parsed = strtoul(token, &end, 10);
    if (errno || *end || parsed > 2147483647ul) return false;
    value = parsed;
    return true;
}
bool Number(const char* token, float& value)
{
    if (!token[0]) return false;
    char* end;
    value = strtof(token, &end);
    return !*end && std::isfinite(value);
}
int Tokens(char* text, char* tokens[], int maximum)
{
    int count = 0;
    char* save = nullptr;
    for (char* token = strtok_r(text, " \t", &save); token; token = strtok_r(nullptr, " \t", &save)) {
        if (count == maximum) return -1;
        tokens[count++] = token;
    }
    return count;
}
bool ValidAction(const char* command)
{
    if (strchr("&@>", command[0])) {
        char text[128];
        strcpy(text, command + 1);
        char* position = text;
        float values[7];
        int count = 0;
        for (;;) {
            if (count >= 7) return false;
            char* comma = strchr(position, ',');
            if (comma) *comma = 0;
            if (!Number(position, values[count++])) return false;
            if (!comma) break;
            position = comma + 1;
        }
        if (count != 6 && count != 7) return false;
        if (count == 7 && (values[6] <= 0 || values[6] > 100)) return false;
        if (command[0] != '@') {
            for (int n = 1; n <= 6; ++n)
                if (values[n - 1] < dummy.motorJ[n]->angleLimitMin || values[n - 1] > dummy.motorJ[n]->angleLimitMax) return false;
        }
        return true;
    }
    if (Is(command, "!HD_POS")) {
        char text[128]; strcpy(text, command);
        char* tokens[3];
        uint32_t percent;
        return Tokens(text, tokens, 3) == 2 && Unsigned(tokens[1], percent) && percent <= 100;
    }
    return !strchr(command, ' ') && !strchr(command, '\t');
}
int Parameter(const char* text)
{
    char key[4] = {};
    size_t size = strlen(text);
    if (size > 2) return -1;
    for (size_t i = 0; i < size; ++i) key[i] = toupper(static_cast<unsigned char>(text[i]));
    if (!strcmp(key, "KP") || !strcmp(key, "P")) return 0;
    if (!strcmp(key, "KI") || !strcmp(key, "I")) return 1;
    if (!strcmp(key, "KD") || !strcmp(key, "D")) return 2;
    if (!strcmp(key, "KV") || !strcmp(key, "V")) return 3;
    return -1;
}
const char* const parameterNames[] = {"KP", "KI", "KD", "KV"};
const uint8_t parameterMasks[] = {1, 4, 8, 2};
void SetParameter(CtrlStepMotor* motor, int parameter, int32_t value)
{
    switch (parameter) {
        case 0: motor->SetDceKp(value); break;
        case 1: motor->SetDceKi(value); break;
        case 2: motor->SetDceKd(value); break;
        case 3: motor->SetDceKv(value); break;
    }
}
int32_t Value(CtrlStepMotor* motor, int parameter)
{
    switch (parameter) {
        case 0: return motor->dceKp;
        case 1: return motor->dceKi;
        case 2: return motor->dceKd;
        default: return motor->dceKv;
    }
}
uint8_t ReadParameters(CtrlStepMotor* motor, int parameter)
{
    if (parameter < 0) motor->QueryDceAll();
    else {
        switch (parameter) {
            case 0: motor->QueryDceKp(); break;
            case 1: motor->QueryDceKi(); break;
            case 2: motor->QueryDceKd(); break;
            case 3: motor->QueryDceKv(); break;
        }
    }
    uint8_t expected = parameter < 0 ? 15 : parameterMasks[parameter];
    for (int wait = 0; wait < 50; ++wait) {
        if ((motor->dceReceivedMask & expected) == expected) break;
        osDelay(10);
    }
    return motor->dceReceivedMask & expected;
}
bool Saved(CtrlStepMotor* motor)
{
    for (int attempt = 0; attempt < 20 && dummy.robotState == DummyRobot::STATE_NORMAL; ++attempt) {
        motor->QueryConfigSaved();
        for (int wait = 0; wait < 10 && !motor->configQueryDone; ++wait) osDelay(5);
        if (motor->configQueryDone && motor->configSaved) return true;
        osDelay(10);
    }
    return false;
}

#ifdef J1_SOUND_EXPERIMENT
bool AllowedInJ1Test(const char* command)
{
    return Query(command) || Is(command, "!J1_TEST_START") || Is(command, "!J1_SOUND") ||
        Is(command, "!RGB_ON") || Is(command, "!RGB_OFF") || Is(command, "!RGB_MODE") ||
        Is(command, "!RGB_CUSTOM");
}
// 临时实验入口仅借用维护互斥，不进入运动队列，不执行整臂归位。
bool SoundExchange(uint8_t operation, uint16_t frequency = 0, uint16_t duration = 0,
                   uint16_t amplitude = 0)
{
    static uint8_t sequence = 0;
    if (++sequence == 0) ++sequence;
    auto* motor = dummy.motorJ[1];
    motor->soundAckToken = 0;
    motor->soundDiagnosticToken = 0;
    if (!motor->SendSoundTest(operation, sequence, frequency, duration, amplitude)) return false;
    const uint32_t start = HAL_GetTick();
    while ((operation == 6 ? motor->soundDiagnosticToken : motor->soundAckToken) != sequence &&
           HAL_GetTick() - start < 150) osDelay(2);
    return (operation == 6 ? motor->soundDiagnosticToken : motor->soundAckToken) == sequence;
}
void StartJ1Test(StreamSink& output, int count)
{
    if (count != 1) { Respond(output, "J1TestStart Error: bad_args"); return; }
    if (dummy.IsEnabled()) { Respond(output, "J1TestStart Error: arm_enabled"); return; }
    auto* motor = dummy.motorJ[1];
    const uint32_t epoch = dummy.stopEpoch;
    if (!SoundExchange(2) || motor->soundCapability != 3) {
        Respond(output, "J1TestStart Error: require_exp3_board"); return;
    }
    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    const bool allowed = dummy.stopEpoch == epoch && dummy.robotState == DummyRobot::STATE_NORMAL &&
        !dummy.pauseRequested;
    if (allowed) dummy.j1TestMode = true;
    __set_PRIMASK(mask);
    if (!allowed) { Respond(output, "J1TestStart Error: interrupted STATE:%s", dummy.StateName()); return; }
    // 锁存隔离后才允许单节点使能；失败也保留隔离，防止串入整臂动作。
    if (!SoundExchange(3) || motor->soundResult != 0) {
        motor->SendSoundTest(0, 0);
        Respond(output, "J1TestStart Error: hold_failed CODE:%u STATE:%s", motor->soundResult, dummy.StateName());
        return;
    }
    const uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < 500 && dummy.stopEpoch == epoch &&
           dummy.robotState == DummyRobot::STATE_NORMAL && !dummy.pauseRequested) {
        if (SoundExchange(2) && motor->soundHoldActive && motor->soundHoldReady) {
            if (dummy.stopEpoch == epoch && dummy.robotState == DummyRobot::STATE_NORMAL && !dummy.pauseRequested) {
                Respond(output, "J1TestStart OK MODE:J1_ONLY HOLD:1 READY:1 STATE:NORMAL");
                return;
            }
        }
        osDelay(10);
    }
    motor->SendSoundTest(0, 0);
    Respond(output, "J1TestStart Error: not_ready STATE:%s", dummy.StateName());
}
void SoundTest(StreamSink& output, char* arguments[], int count, bool query)
{
    static const char* const waves[] = {"D_SINE", "Q_SINE", "Q_SQUARE"};
    auto* motor = dummy.motorJ[1];
    if (query) {
        if (count != 1) { Respond(output, "J1Sound Error: bad_args"); return; }
        if (!SoundExchange(2) || !SoundExchange(6)) {
            Respond(output, "J1Sound Error: no_experiment_ack"); return;
        }
        Respond(output, "J1Sound OK EXP:%u SOUND:%u STATE:%s MODE:%s HOLD:%u READY:%u WAVE:%s CMD_TICKS:%u PEAK_CMD_MA:%u",
            motor->soundCapability, motor->soundState, dummy.StateName(),
            dummy.j1TestMode ? "J1_ONLY" : "ARM", motor->soundHoldActive, motor->soundHoldReady,
            waves[motor->soundWave <= 2 ? motor->soundWave : 0], motor->soundInjectedTicks,
            motor->soundInjectedPeak);
        return;
    }
    uint32_t frequency = 0, duration = 0, amplitude = 0;
    uint8_t wave = 0;
    if (count == 5) {
        for (char* p = arguments[4]; *p; ++p) *p = toupper(static_cast<unsigned char>(*p));
        if (!strcmp(arguments[4], "Q_SINE")) wave = 1;
        else if (!strcmp(arguments[4], "Q_SQUARE")) wave = 2;
        else if (strcmp(arguments[4], "D_SINE")) {
            Respond(output, "J1Sound Error: bad_wave"); return;
        }
    }
    if ((count != 4 && count != 5) || !Unsigned(arguments[1], frequency) || !Unsigned(arguments[2], duration) ||
        !Unsigned(arguments[3], amplitude) || frequency < 400 || frequency > 2000 ||
        duration < 50 || duration > 500 || amplitude < 25 || amplitude > 250) {
        Respond(output, "J1Sound Error: bad_args"); return;
    }
    if (!dummy.j1TestMode && (!dummy.IsEnabled() || !dummy.IsHomed())) {
        Respond(output, "J1Sound Error: require_enabled_and_homed"); return;
    }
    // 先探测专用子板协议，避免向未更新的子板假报成功。
    if (!SoundExchange(2)) { Respond(output, "J1Sound Error: no_experiment_ack"); return; }
    if (motor->soundCapability != 3 ||
        (dummy.j1TestMode && (!motor->soundHoldActive || !motor->soundHoldReady))) {
        Respond(output, "J1Sound Error: require_j1_hold STATE:%s", dummy.StateName()); return;
    }
    const uint32_t epoch = dummy.stopEpoch;
    const bool received = SoundExchange(wave == 0 ? 1 : wave + 3, frequency, duration, amplitude);
    if (!received || motor->soundResult != 0 || dummy.stopEpoch != epoch ||
        dummy.robotState != DummyRobot::STATE_NORMAL) {
        const unsigned result = motor->soundResult;
        motor->SendSoundTest(0, 0);
        Respond(output, "J1Sound Error: start_failed ACK:%u CODE:%u STATE:%s", received, result, dummy.StateName());
        return;
    }
    Respond(output, "J1Sound Accepted HZ:%lu MS:%lu MA:%lu WAVE:%s", frequency, duration, amplitude, waves[wave]);
    const uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < duration + 10 && dummy.stopEpoch == epoch &&
           dummy.robotState == DummyRobot::STATE_NORMAL && !dummy.pauseRequested) osDelay(2);
    if (dummy.stopEpoch != epoch || dummy.robotState != DummyRobot::STATE_NORMAL || dummy.pauseRequested) {
        motor->SendSoundTest(0, 0);
        Respond(output, "J1Sound Error: cancelled STATE:%s", dummy.StateName()); return;
    }
    if (!SoundExchange(2) || motor->soundState != 2 || !SoundExchange(6) ||
        motor->soundInjectedTicks == 0 || motor->soundInjectedPeak == 0) {
        motor->SendSoundTest(0, 0);
        Respond(output, "J1Sound Error: incomplete SOUND:%u STATE:%s", motor->soundState, dummy.StateName());
        return;
    }
    if (dummy.stopEpoch != epoch || dummy.robotState != DummyRobot::STATE_NORMAL || dummy.pauseRequested) {
        motor->SendSoundTest(0, 0);
        Respond(output, "J1Sound Error: cancelled STATE:%s", dummy.StateName()); return;
    }
    Respond(output, "J1Sound OK SOUND:%u STATE:%s WAVE:%s CMD_TICKS:%u PEAK_CMD_MA:%u",
        motor->soundState, dummy.StateName(), waves[wave], motor->soundInjectedTicks, motor->soundInjectedPeak);
}
#endif

void Execute(const Message& message)
{
    TransactionSink output(Output(message.channel), message.request);
    const char* tag = Tag(message.text);
    bool query = Query(message.text);
    if (!query && dummy.robotState != DummyRobot::STATE_NORMAL) {
        Respond(output, "%s Error: state=%s", tag, dummy.StateName());
        return;
    }
#ifdef J1_SOUND_EXPERIMENT
    if (dummy.j1TestMode && !AllowedInJ1Test(message.text)) {
        Respond(output, "%s Error: j1_test_only STATE:%s", tag, dummy.StateName()); return;
    }
#endif
    char text[128];
    memcpy(text, message.text, sizeof(text));
    char* arguments[8];
    int count = Tokens(text, arguments, 8);
    if (count < 1) { Respond(output, "%s Error: bad_args", tag); return; }
    const char* command = arguments[0];
    uint32_t node = 0;
    if (count >= 2) Unsigned(arguments[1], node);
    if (query) {
#ifdef J1_SOUND_EXPERIMENT
        if (!strcmp(command, "#J1_SOUND")) { SoundTest(output, arguments, count, true); return; }
#endif
        if (!strcmp(command, "#STATUS")) {
            Respond(output, "Status OK STATE:%s CAN_RX:%lu CAN_DROP:%lu", dummy.StateName(), dummy.canRxTotal, dummy.canDroppedFrames);
            Respond(output, "FW:%s BUILD:%s %s", CONFIG_FW_VERSION_TEXT, __DATE__, __TIME__);
            const auto fault = dummy.motionFault;
            Respond(output, "FAULT:%s NODE:%lu AGE_MS:%lu ERROR_DEG:%.2f FINISHED:%lu STALLED:%lu",
                    fault.reason, fault.node, fault.feedbackAge, fault.error, fault.finished, fault.stalled);
            Respond(output, "ENABLED:%u HOMED:%u ACTIVE:%u QUEUE:%lu", dummy.IsEnabled(), dummy.IsHomed(), dummy.TaskActive(), 16 - dummy.commandHandler.GetSpace());
#ifdef J1_SOUND_EXPERIMENT
            Respond(output, "J1_TEST_MODE:%u", dummy.j1TestMode);
#endif
            for (int n = 1; n <= 7; ++n) {
                auto* motor = dummy.motorJ[n];
                const uint32_t received = motor->lastResponseTick;
                const uint32_t age = HAL_GetTick() - received;
                Respond(output, "%s[%d] %s age=%lums angle=%.2f finished=%u stalled=%u", n == 7 ? "HD" : "J", n,
                    motor->isOnline ? "OL" : "NL", motor->isOnline ? static_cast<unsigned long>(age) : 0xfffffffful,
                    motor->angle, motor->state == CtrlStepMotor::FINISH, motor->isStalled);
            }
            Respond(output, "Status End");
        } else if (!strcmp(command, "#GETJPOS") || !strcmp(command, "#GETLPOS")) {
            dummy.UpdateJointAngles();
            dummy.hand2->UpdateAngle();
            osDelay(30);
            if (!strcmp(command, "#GETJPOS")) {
                auto& j = dummy.currentJoints;
                Respond(output, "GetJPos OK %.2f %.2f %.2f %.2f %.2f %.2f H:%d", j.a[0], j.a[1], j.a[2], j.a[3], j.a[4], j.a[5], dummy.hand2->GetState());
            } else {
                dummy.UpdateJointPose6D();
                auto& p = dummy.currentPose6D;
                Respond(output, "GetLPos OK %.2f %.2f %.2f %.2f %.2f %.2f H:%d", p.X, p.Y, p.Z, p.A, p.B, p.C, dummy.hand2->GetState());
            }
        } else if (!strcmp(command, "#GETSPEED")) {
            Respond(output, "GetSpeed OK SPEED:%.2f", dummy.GetJointSpeed());
        } else if (!strcmp(command, "#ONLINE")) {
            if (count == 1) {
                bool online = dummy.CheckAllNodesOnline();
                Respond(output, "Online OK %s", online ? "all_online" : "has_offline");
                for (int n = 1; n <= 7; ++n) Respond(output, "%s[%d] %s", n == 7 ? "HAND" : "J", n, dummy.motorJ[n]->isOnline ? "OL" : "NL");
            } else if (count == 2 && node >= 1 && node <= 7) {
                bool online = dummy.CheckNodeOnline(node);
                Respond(output, "Online OK node=%lu %s", node, online ? "OL" : "NL");
            } else Respond(output, "Online Error: bad_args");
            Respond(output, "Online End");
        } else {
            bool all = count == 1 || (count == 2 && (!strcmp(arguments[1], "all") || !strcmp(arguments[1], "ALL")));
            int parameter = count == 3 ? Parameter(arguments[2]) : -1;
            if ((!all && (node < 1 || node > 7)) || count > 3 || (count == 3 && parameter < 0)) {
                Respond(output, "%s Error: bad_args", tag);
                return;
            }
            for (uint32_t n = all ? 1 : node; n <= (all ? 7 : node); ++n) {
                auto* motor = dummy.motorJ[n];
                uint8_t received = ReadParameters(motor, parameter);
                uint8_t expected = parameter < 0 ? 15 : parameterMasks[parameter];
                Respond(output, "J%lu  %s:", n, received == expected ? "OL" : "NL");
                for (int p = 0; p < 4; ++p) {
                    if ((parameter < 0 || parameter == p) && (received & parameterMasks[p]))
                        Respond(output, "%s:%ld", parameterNames[p], static_cast<long>(Value(motor, p)));
                }
            }
            Respond(output, "%s OK STATE:%s", tag, dummy.StateName());
        }
        return;
    }

    // RGB 不进入运动队列，也不触发 HOME。
    if (!strcmp(command, "!RGB_ON") && count == 1) { dummy.SetRGBEnable(true); Respond(output, "RGB_ON OK"); return; }
    if (!strcmp(command, "!RGB_OFF") && count == 1) { dummy.SetRGBEnable(false); Respond(output, "RGB_OFF OK"); return; }
    if (!strcmp(command, "!RGB_MODE") && count == 2 && Unsigned(arguments[1], node) && node <= 11) {
        dummy.SetRGBMode(node); Respond(output, "RGB_MODE OK mode=%lu", node); return;
    }
    if (!strcmp(command, "!RGB_CUSTOM") && count == 4) {
        uint32_t colors[3];
        bool valid = true;
        for (int i = 0; i < 3; ++i) if (!Unsigned(arguments[i + 1], colors[i]) || colors[i] > 255) valid = false;
        if (valid) {
            rgb.customR = colors[0]; rgb.customG = colors[1]; rgb.customB = colors[2];
            dummy.SetRGBMode(8); Respond(output, "RGB_CUSTOM OK"); return;
        }
    }
    if (!strcmp(command, "!DISABLE")) {
        Respond(output, "%s Error: use !START or !CLOSE", tag); return;
    }
    struct MaintenanceGuard {
        bool owned = false;
        ~MaintenanceGuard() { if (owned) dummy.maintenanceActive = false; }
    } guard;
    uint32_t interruptMask = __get_PRIMASK();
    __disable_irq();
    if (!dummy.scheduledActions && !dummy.TaskActive() && dummy.robotState == DummyRobot::STATE_NORMAL) {
        dummy.maintenanceActive = true;
        guard.owned = true;
    }
    __set_PRIMASK(interruptMask);
    if (!guard.owned) {
        Respond(output, "%s Error: busy", tag); return;
    }
#ifdef J1_SOUND_EXPERIMENT
    if (!strcmp(command, "!J1_TEST_START")) { StartJ1Test(output, count); return; }
    if (!strcmp(command, "!J1_SOUND")) { SoundTest(output, arguments, count, false); return; }
#endif
    if (!strcmp(command, "!HD_I") && count == 2) {
        float current;
        if (Number(arguments[1], current) && current > 0 && current <= 2) {
            dummy.hand2->current = current; Respond(output, "HD_I OK current=%.2f", current); return;
        }
    }
    bool setPid = !strcmp(command, "#SETPID") || !strcmp(command, "SETPID");
    bool legacyPid = strncmp(command, "#SET_DCE_", 9) == 0;
    if (setPid || legacyPid) {
        int first = -1;
        uint32_t values[4] = {};
        bool valid = node >= 1 && node <= 7;
        if (setPid && count == 6) {
            for (int p = 0; p < 4; ++p) if (!Unsigned(arguments[p + 2], values[p])) valid = false;
        } else if (setPid && count == 4) {
            first = Parameter(arguments[2]);
            valid = valid && first >= 0;
            if (first >= 0 && !Unsigned(arguments[3], values[first])) valid = false;
        } else if (legacyPid && count == 3) {
            first = Parameter(command + 9);
            valid = valid && first >= 0;
            if (first >= 0 && !Unsigned(arguments[2], values[first])) valid = false;
        } else valid = false;
        if (!valid) { Respond(output, "%s Error: bad_args", tag); return; }
        auto* motor = dummy.motorJ[node];
        for (int p = 0; p < 4; ++p) if (first < 0 || p == first) {
            if (dummy.robotState != DummyRobot::STATE_NORMAL) break;
            SetParameter(motor, p, values[p]);
            if (!Saved(motor)) { Respond(output, "%s Error: save_failed state=%s", tag, dummy.StateName()); return; }
        }
        bool saved = Saved(motor);
        uint8_t received = ReadParameters(motor, first);
        for (int p = 0; p < 4; ++p) if (first < 0 || first == p) {
            if (!(received & parameterMasks[p]) || Value(motor, p) != static_cast<int32_t>(values[p])) saved = false;
        }
        if (saved && dummy.robotState == DummyRobot::STATE_NORMAL) Respond(output, "%s OK node=%lu", tag, node);
        else Respond(output, "%s Error: verify_failed state=%s", tag, dummy.StateName());
        return;
    }
    if (!strcmp(command, "#OFFSET_J") && count == 2) {
        bool all = !strcmp(arguments[1], "all") || !strcmp(arguments[1], "ALL");
        if (!all && (node < 1 || node > 7)) { Respond(output, "OffsetJ Error: bad_args"); return; }
        dummy.InvalidateHome();
        for (uint32_t n = all ? 1 : node; n <= (all ? 7 : node); ++n) {
            if (dummy.robotState != DummyRobot::STATE_NORMAL) { Respond(output, "OffsetJ Error: state=%s", dummy.StateName()); return; }
            if (!dummy.motorJ[n]->ApplyPositionAsHome()) {
                Respond(output, "OffsetJ Error: send_failed node=%lu state=%s", n, dummy.StateName()); return;
            }
            if (!Saved(dummy.motorJ[n])) { Respond(output, "OffsetJ Error: save_failed node=%lu state=%s", n, dummy.StateName()); return; }
        }
        if (dummy.robotState != DummyRobot::STATE_NORMAL) { Respond(output, "OffsetJ Error: state=%s", dummy.StateName()); return; }
        Respond(output, "OffsetJ OK %s", all ? "all rebooting" : "saved");
        if (all) {
            osDelay(100);
            if (dummy.robotState != DummyRobot::STATE_NORMAL) return;
            dummy.motorJ[ALL]->Reboot();
            osDelay(100);
            if (dummy.robotState == DummyRobot::STATE_NORMAL) HAL_NVIC_SystemReset();
        }
        return;
    }
    if (!strcmp(command, "#CMDMODE") && count == 2 && node >= 1 && node <= 4) {
        dummy.SetCommandMode(node); Respond(output, "CmdMode OK mode=%lu", node); return;
    }
    if (!strcmp(command, "#REBOOT") && (count == 1 || (count == 2 && Unsigned(arguments[1], node) && node <= 7))) {
        Respond(output, "Reboot OK");
        osDelay(100);
        if (dummy.robotState != DummyRobot::STATE_NORMAL) return;
        if (count == 1) dummy.motorJ[ALL]->Reboot();
        else if (node) dummy.motorJ[node]->Reboot();
        if (count == 1 || !node) { osDelay(100); if (dummy.robotState == DummyRobot::STATE_NORMAL) HAL_NVIC_SystemReset(); }
        return;
    }
    if (count == 3 && node >= 1 && node <= 7) {
        float value;
        if (Number(arguments[2], value)) {
            bool valid = false;
            if (!strcmp(command, "#ACC_J") && value > 0) {
                dummy.motorJ[node]->SetAcceleration(value);
                valid = true;
            } else if (!strcmp(command, "#SPEED_J") && value >= 0) {
                dummy.motorJ[node]->SetVelocityLimit(value);
                valid = true;
            } else if (!strcmp(command, "#I_LIMIT_J") && value >= 0) {
                dummy.motorJ[node]->SetCurrentLimit(value);
                valid = true;
            }
            if (valid) { Respond(output, "%s OK node=%lu", tag, node); return; }
        }
    }
    Respond(output, "%s Error: bad_args", tag);
}
void MaintenanceThread(void*)
{
    for (;;) ProcessAsciiCommand(osWaitForever);
}
void Receive(const char* input, size_t length, StreamSink& baseOutput)
{
    if (!length) return;
    Message message{};
    if (length > 2 && input[0] == 'T' && input[1] == ':') {
        const char* space = static_cast<const char*>(memchr(input, ' ', length));
        if (!space || space - input > 12) { Respond(baseOutput, "Command Error: bad_args"); return; }
        char number[12] = {};
        memcpy(number, input + 2, space - input - 2);
        if (!Unsigned(number, message.request) || !message.request) { Respond(baseOutput, "Command Error: bad_args"); return; }
        size_t prefix = space + 1 - input;
        input += prefix;
        length -= prefix;
    }
    TransactionSink output(baseOutput, message.request);
    if (!length || length >= sizeof(Message::text)) { Respond(output, "Command Error: too_long"); return; }
    memcpy(message.text, input, length);
    // 仅规范命令名的大小写，参数保留原文；禁止把多行当作一条指令。
    for (size_t i = 0; i < length; ++i) {
        if (message.text[i] == '\r' || message.text[i] == '\n' || message.text[i] == 0) { Respond(output, "Command Error: bad_args"); return; }
    }
    size_t end = length;
    while (end && (message.text[end - 1] == ' ' || message.text[end - 1] == '\t')) message.text[--end] = 0;
    for (size_t i = 0; i < end && message.text[i] != ' ' && message.text[i] != '\t'; ++i)
        message.text[i] = toupper(static_cast<unsigned char>(message.text[i]));
    const char* command = message.text;
    if (!strcmp(command, "!STOP")) {
        dummy.commandHandler.EmergencyStop();
        Respond(output, "Stop OK STATE:%s", dummy.StateName()); return;
    }
    if (!strcmp(command, "!PAUSE")) {
        bool accepted = dummy.commandHandler.Pause();
        if (accepted) Respond(output, "Pause OK STATE:%s", dummy.StateName());
        else Respond(output, "Pause Error: state=%s", dummy.StateName());
        return;
    }
    if (!strcmp(command, "!RESUME")) {
        const char* previous = dummy.StateName();
        dummy.commandHandler.Resume();
        Respond(output, "Resume OK FROM:%s STATE:%s", previous, dummy.StateName()); return;
    }
    const char* tag = Tag(command);
    if (!Query(command) && dummy.robotState != DummyRobot::STATE_NORMAL) {
        Respond(output, "%s Error: state=%s", tag, dummy.StateName()); return;
    }
#ifdef J1_SOUND_EXPERIMENT
    if (dummy.j1TestMode && !AllowedInJ1Test(command)) {
        Respond(output, "%s Error: j1_test_only STATE:%s", tag, dummy.StateName()); return;
    }
#endif
    message.channel = output.channelType;
    if (Action(command)) {
        if (!ValidAction(command)) { Respond(output, "%s Error: bad_args", tag); return; }
        uint32_t space = dummy.commandHandler.Push(command, output.channelType, message.request);
        if (space == 0xfd) Respond(output, "%s Error: not_enabled", tag);
        else if (space == 0xfc) Respond(output, "%s Error: busy", tag);
        else if (space == 0xfe) Respond(output, "%s Error: state=%s", tag, dummy.StateName());
        else if (space >= 0xfb) Respond(output, "%s Error: queue_full", tag);
        else Respond(output, "%s Accepted Q:%lu", tag, 16 - space);
        return;
    }
    if (!maintenanceQueue || osMessageQueuePut(maintenanceQueue, &message, 0, 0) != osOK)
        Respond(output, "%s Error: queue_full", tag);
}
}

bool ProcessAsciiCommand(uint32_t timeout)
{
    Message message;
    if (osMessageQueueGet(maintenanceQueue, &message, nullptr, timeout) != osOK) return false;
    Execute(message);
    return true;
}

void InitAsciiCommands()
{
    maintenanceQueue = osMessageQueueNew(16, sizeof(Message), nullptr);
    if (!maintenanceQueue)
    {
        HandleRuntimeInitFailure();
    }
    const osThreadAttr_t attributes = {
        .name = "QueryConfigTask",
        .stack_size = 3072,
        .priority = osPriorityNormal,
    };
    osThreadId_t maintenanceTaskHandle = osThreadNew(MaintenanceThread, nullptr, &attributes);
    if (!maintenanceTaskHandle)
    {
        HandleRuntimeInitFailure();
    }
}
void OnUsbAsciiCmd(const char* command, size_t length, StreamSink& output) { Receive(command, length, output); }
void OnUart4AsciiCmd(const char* command, size_t length, StreamSink& output) { Receive(command, length, output); }
void OnUart5AsciiCmd(const char* command, size_t length, StreamSink& output) { Receive(command, length, output); }
