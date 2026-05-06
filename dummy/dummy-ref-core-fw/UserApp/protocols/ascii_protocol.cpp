#include "common_inc.h"
#include <cstring>

extern DummyRobot dummy;
extern RGB rgb;

// 精确前缀匹配：匹配 _cmd 以 _kw 开头且后一个字符为结束符/空白
// 避免 "STOP" 误中 "!STOPX" 之类。
static inline bool MatchExact(const char* _cmd, const char* _kw)
{
    size_t n = std::strlen(_kw);
    if (std::strncmp(_cmd, _kw, n) != 0) return false;
    char c = _cmd[n];
    return c == '\0' || c == '\r' || c == '\n' || c == ' ' || c == '\t';
}

// 机器人状态码 → 文字
static inline const char* StateName(int s)
{
    switch (s)
    {
        case DummyRobot::STATE_NORMAL:          return "Normal";
        case DummyRobot::STATE_EMERGENCY_STOP:  return "Stoping";
        case DummyRobot::STATE_PAUSED:          return "Pauseing";
        default:                                return "Unknown";
    }
}

void OnUsbAsciiCmd(const char* _cmd, size_t _len, StreamSink &_responseChannel)
{
    uint8_t  i;
    /*---------------------------- ↓ Add Your CMDs Here ↓ -----------------------------*/
    if (_cmd[0] == '!' )
    {
        // 最高优先级：急停 / 暂停 / 恢复（精确前缀匹配）
        if (MatchExact(_cmd, "!STOP"))
        {
            dummy.commandHandler.EmergencyStop();
            Respond(_responseChannel, "Stop OK");
            Respond(_responseChannel, "STATE:%s QUEUE:%lu",
                    StateName((int)dummy.robotState),
                    (unsigned long)dummy.commandHandler.GetSpace());
            return;
        }
        if (MatchExact(_cmd, "!PAUSE"))
        {
            if (!dummy.commandHandler.Pause())
            {
                Respond(_responseChannel, "Pause Error: state=%s",
                        StateName((int)dummy.robotState));
            }
            else
            {
                Respond(_responseChannel, "Pause OK");
                Respond(_responseChannel, "STATE:%s QUEUE:%lu",
                        StateName((int)dummy.robotState),
                        (unsigned long)dummy.commandHandler.GetSpace());
            }
            return;
        }
        if (MatchExact(_cmd, "!RESUME"))
        {
            dummy.commandHandler.Resume();
            Respond(_responseChannel, "Resume OK");
            Respond(_responseChannel, "STATE:%s QUEUE:%lu",
                    StateName((int)dummy.robotState),
                    (unsigned long)dummy.commandHandler.GetSpace());
            return;
        }
        // !CLOSE 必须在 find("CLOSE") 手爪分支之前精确匹配，避免被误入手爪闭合分支
        if (MatchExact(_cmd, "!CLOSE"))
        {
            if (dummy.robotState != DummyRobot::STATE_NORMAL)
            {
                Respond(_responseChannel, "Close Error: state=%s",
                        StateName((int)dummy.robotState));
            }
            else
            {
                dummy.PostSysCmd(DummyRobot::SYSCMD_SHUTDOWN, _responseChannel.channelType);
            }
            return;
        }

        // 以下均精确匹配（MatchExact 允许后跟空格/\r/\n/\0/\t）
        if (false)
        {
            // placeholder to keep the original else-if chain below intact
        } else if (MatchExact(_cmd, "!START"))
        {
            // 工作前在线检查
            bool allOk = dummy.CheckAllNodesOnline();
            if (!allOk)
            {
                // 单行汇总离线节点
                char buf[80]; buf[0] = '\0';
                size_t off = 0;
                for (int n = 1; n <= 6; n++)
                    if (!dummy.motorJ[n]->isOnline)
                        off += snprintf(buf + off, sizeof(buf) - off, "J[%d] ", n);
                if (dummy.hand2 != nullptr && !dummy.hand2->isOnline)
                    off += snprintf(buf + off, sizeof(buf) - off, "HAND[7] ");
                Respond(_responseChannel, "Start Error: offline=%s", buf);
            }
            else
            {
                dummy.PostSysCmd(DummyRobot::SYSCMD_STARTUP, _responseChannel.channelType);
            }
        } else if (MatchExact(_cmd, "!HOME"))
        {
            // !HOME 必须已使能（不自动使能以确保安全）
            if (!dummy.IsEnabled())
            {
                Respond(_responseChannel, "Home Error: not_enabled");
            }
            else
            {
                dummy.PostSysCmd(DummyRobot::SYSCMD_HOME, _responseChannel.channelType);
            }
        } else if (MatchExact(_cmd, "!CALIBRATION"))
        {
            dummy.CalibrateHomeOffset();
            Respond(_responseChannel, "Calibration OK");
        } else if (MatchExact(_cmd, "!RESET"))
        {
            // !RESET 仅回零点姿态，不失能（要失能请用 !CLOSE）；需已使能
            if (!dummy.IsEnabled())
            {
                Respond(_responseChannel, "Reset Error: not_enabled");
            }
            else
            {
                dummy.PostSysCmd(DummyRobot::SYSCMD_RESET, _responseChannel.channelType);
            }
        }
        // 手爪控制（hand2 / StepHand）—— 失能时拒绝运动命令
        else if (MatchExact(_cmd, "!HD_O"))
        {
            if (!dummy.hand2->isEnabled())
            {
                Respond(_responseChannel, "HD_O Error: hand disabled (use !HD_EN or !START)");
            }
            else
            {
                dummy.hand2->SetAngleWithCurrentLimit(-1);
                dummy.OnHandActionRGBHook(true);  // RGB 联动 hook（预留）
                Respond(_responseChannel, "HD_O OK");
            }
        }
        else if (MatchExact(_cmd, "!HD_C"))
        {
            if (!dummy.hand2->isEnabled())
            {
                Respond(_responseChannel, "HD_C Error: hand disabled (use !HD_EN or !START)");
            }
            else
            {
                dummy.hand2->SetAngleWithCurrentLimit(1);
                dummy.OnHandActionRGBHook(false); // RGB 联动 hook（预留）
                Respond(_responseChannel, "HD_C OK");
            }
        }
        // RGB 控制命令
        else if (MatchExact(_cmd, "!RGB_ON"))
        {
            dummy.SetRGBEnable(true);
            Respond(_responseChannel, "RGB_ON OK");
        }
        else if (MatchExact(_cmd, "!RGB_OFF"))
        {
            dummy.SetRGBEnable(false);
            Respond(_responseChannel, "RGB_OFF OK");
        }
        else if (MatchExact(_cmd, "!RGB_MODE"))
        {
            uint32_t mode;
            if (sscanf(_cmd, "!RGB_MODE %lu", &mode) == 1)
            {
                if (mode <= 11)
                {
                    dummy.SetRGBMode(mode);
                    Respond(_responseChannel, "RGB_MODE OK mode=%lu", mode);
                }
                else
                {
                    Respond(_responseChannel, "RGB_MODE Error: mode must be 0-11");
                }
            }
            else
            {
                Respond(_responseChannel, "RGB_MODE Error: use !RGB_MODE <0-11>");
            }
        }
        else if (MatchExact(_cmd, "!RGB_CUSTOM"))
        {
            uint32_t r, g, b;
            if (sscanf(_cmd, "!RGB_CUSTOM %lu %lu %lu", &r, &g, &b) == 3 && r <= 255 && g <= 255 && b <= 255)
            {
                rgb.customR = (uint8_t)r; rgb.customG = (uint8_t)g; rgb.customB = (uint8_t)b;
                dummy.SetRGBMode(8);  // 切到自定义
                Respond(_responseChannel, "RGB_CUSTOM OK r=%lu g=%lu b=%lu", r, g, b);
            }
            else
            {
                Respond(_responseChannel, "RGB_CUSTOM Error: use !RGB_CUSTOM <r> <g> <b> (0-255)");
            }
        }
        else if (MatchExact(_cmd, "!HD_EN"))
        {
            dummy.hand2->SetEnable(true);
            dummy.hand2->UpdateAngle();
            Respond(_responseChannel, "HD_EN OK enabled=%lu angle=%.2f",
                    (unsigned long)dummy.hand2->isEnabled(), dummy.hand2->angle);
        }
        else if (MatchExact(_cmd, "!HD_DIS"))
        {
            dummy.hand2->SetEnable(false);
            dummy.hand2->UpdateAngle();
            Respond(_responseChannel, "HD_DIS OK enabled=%lu angle=%.2f",
                    (unsigned long)dummy.hand2->isEnabled(), dummy.hand2->angle);
        }
        else if (MatchExact(_cmd, "!HD_ZERO"))
        {
            dummy.hand2->HandCalibration();
            dummy.hand2->UpdateAngle();
            Respond(_responseChannel, "HD_ZERO OK MIN:%.2f MAX:%.2f",
                    dummy.hand2->ClosedAngle, dummy.hand2->OpenedAngle);
        }
        else if (MatchExact(_cmd, "!HD_POS"))
        {
            uint32_t pos;
            if (!dummy.hand2->isEnabled())
            {
                Respond(_responseChannel, "HD_POS Error: hand disabled (use !HD_EN or !START)");
            }
            else if (sscanf(_cmd, "!HD_POS %lu", &pos) == 1)
            {
                if (pos <= 100)
                {
                    dummy.hand2->SetPercentToAngle(static_cast<float>(pos));
                    dummy.hand2->UpdateAngle();
                    Respond(_responseChannel, "HD_POS OK pos=%lu angle=%.2f",
                            (unsigned long)pos, dummy.hand2->angle);
                }
                else
                {
                    Respond(_responseChannel, "HD_POS Error: out of range (0-100)");
                }
            }
            else
            {
                Respond(_responseChannel, "HD_POS Error: use !HD_POS <0-100>");
            }
        }
        else if (MatchExact(_cmd, "!HD_I"))
        {
            float cu;
            if (sscanf(_cmd, "!HD_I %f", &cu) == 1)
            {
                if (cu > 0 && cu <= 2)
                {
                    dummy.hand2->current = cu;
                    Respond(_responseChannel, "HD_I OK current=%.2f", cu);
                }
                else
                {
                    Respond(_responseChannel, "HD_I Error: out of range (0-2.0)");
                }
            }
            else
            {
                Respond(_responseChannel, "HD_I Error: use !HD_I <0-2.0>");
            }
        }
        else
        {
            Respond(_responseChannel, "Error: unknown command");
        }

    } else if (_cmd[0] == '#')
    {
        if (MatchExact(_cmd, "#GETJPOS"))
        {
            // 手爪状态: angle < 中点 → 开(1), 否则 → 关(0)
            int handState = 0;
            if (dummy.hand2 != nullptr)
            {
                dummy.hand2->UpdateAngle();
                float mid = (dummy.hand2->OpenedAngle + dummy.hand2->ClosedAngle) / 2.0f;
                handState = (dummy.hand2->angle < mid) ? 1 : 0;
            }
            Respond(_responseChannel, "GetJPos OK %.2f %.2f %.2f %.2f %.2f %.2f H:%d",
                    dummy.currentJoints.a[0], dummy.currentJoints.a[1],
                    dummy.currentJoints.a[2], dummy.currentJoints.a[3],
                    dummy.currentJoints.a[4], dummy.currentJoints.a[5],
                    handState);
        } else if (MatchExact(_cmd, "#GETLPOS"))
        {
            dummy.UpdateJointPose6D();
            int handState = 0;
            if (dummy.hand2 != nullptr)
            {
                dummy.hand2->UpdateAngle();
                float mid = (dummy.hand2->OpenedAngle + dummy.hand2->ClosedAngle) / 2.0f;
                handState = (dummy.hand2->angle < mid) ? 1 : 0;
            }
            Respond(_responseChannel, "GetLPos OK %.2f %.2f %.2f %.2f %.2f %.2f H:%d",
                    dummy.currentPose6D.X, dummy.currentPose6D.Y,
                    dummy.currentPose6D.Z, dummy.currentPose6D.A,
                    dummy.currentPose6D.B, dummy.currentPose6D.C,
                    handState);
        }
        else if (MatchExact(_cmd, "#GETSPEED"))
        {
            Respond(_responseChannel, "GetSpeed OK SPEED:%.2f", dummy.GetJointSpeed());
        }
        else if (MatchExact(_cmd, "#SET_DCE_KV"))
        {
            uint32_t kp; uint32_t node;
            sscanf(_cmd, "#SET_DCE_KV %lu %lu", &node, &kp);
            if (node >= 1 && node <= 6){
                dummy.motorJ[node]->SetDceKv(kp);
                Respond(_responseChannel, "SetDceKv OK node=%lu val=%lu", node, kp);
            } else {
                Respond(_responseChannel, "SetDceKv Error: bad node=%lu", node);
            }
        }
        else if (MatchExact(_cmd, "#SET_DCE_KP"))
        {
            uint32_t kp; uint32_t node;
            sscanf(_cmd, "#SET_DCE_KP %lu %lu", &node, &kp);
            if (node >= 1 && node <= 6){
                dummy.motorJ[node]->SetDceKp(kp);
                Respond(_responseChannel, "SetDceKp OK node=%lu val=%lu", node, kp);
            } else {
                Respond(_responseChannel, "SetDceKp Error: bad node=%lu", node);
            }
        } else if (MatchExact(_cmd, "#SET_DCE_KI"))
        {
            uint32_t kp; uint32_t node;
            sscanf(_cmd, "#SET_DCE_KI %lu %lu", &node, &kp);
            if (node >= 1 && node <= 6){
                dummy.motorJ[node]->SetDceKi(kp);
                Respond(_responseChannel, "SetDceKi OK node=%lu val=%lu", node, kp);
            } else {
                Respond(_responseChannel, "SetDceKi Error: bad node=%lu", node);
            }
        } else if (MatchExact(_cmd, "#SET_DCE_KD"))
        {
            uint32_t kp; uint32_t node;
            sscanf(_cmd, "#SET_DCE_KD %lu %lu", &node, &kp);
            if (node >= 1 && node <= 6){
                dummy.motorJ[node]->SetDceKd(kp);
                Respond(_responseChannel, "SetDceKd OK node=%lu val=%lu", node, kp);
            } else {
                Respond(_responseChannel, "SetDceKd Error: bad node=%lu", node);
            }
        } else if (MatchExact(_cmd, "#REBOOT"))
        {
            uint32_t node;
            if (sscanf(_cmd, "#REBOOT %lu", &node) == 1)
            {
                if (node == 0)
                {
                    // 重启 ref 主控板
                    Respond(_responseChannel, "Reboot OK ref (reset in 100ms)");
                    osDelay(100);
                    HAL_NVIC_SystemReset();
                }
                else if (node >= 1 && node <= 6)
                {
                    dummy.motorJ[node]->Reboot();
                    osDelay(20);
                    Respond(_responseChannel, "Reboot OK motor=%lu", node);
                }
                else if (node == 7)
                {
                    if (dummy.hand2 != nullptr)
                    {
                        dummy.hand2->Reboot();
                        osDelay(20);
                        Respond(_responseChannel, "Reboot OK hand");
                    }
                    else
                        Respond(_responseChannel, "Reboot Error: hand not available");
                }
                else
                {
                    Respond(_responseChannel, "Reboot Error: bad node=%lu (0=ref, 1-7=motor/hand)", node);
                }
            }
            else
            {
                // 无参数：重启所有（ref + 全部电机 + 手爪）
                dummy.motorJ[ALL]->Reboot();
                if (dummy.hand2 != nullptr)
                    dummy.hand2->Reboot();
                Respond(_responseChannel, "Reboot OK all (reset in 100ms)");
                osDelay(100);
                HAL_NVIC_SystemReset();
            }
        } else if (MatchExact(_cmd, "#CMDMODE"))
        {
            uint32_t mode;
            sscanf(_cmd, "#CMDMODE %lu", &mode);
            dummy.SetCommandMode(mode);
            Respond(_responseChannel, "CmdMode OK mode=%lu", mode);
        }
        else if (MatchExact(_cmd, "#OFFSET_J"))
        {
            // 支持 #OFFSET_J <1-7> 或 #OFFSET_J all
            char param[16] = {0};
            if (sscanf(_cmd, "#OFFSET_J %15s", param) == 1)
            {
                if (strcmp(param, "all") == 0 || strcmp(param, "ALL") == 0)
                {
                    // 一键设置所有关节偏移
                    for (int n = 1; n <= 6; n++)
                    {
                        dummy.motorJ[n]->ApplyPositionAsHome();
                        osDelay(10);
                    }
                    if (dummy.hand2 != nullptr)
                    {
                        dummy.hand2->ApplyPositionAsHome();
                        osDelay(10);
                    }
                    Respond(_responseChannel, "OffsetJ OK all rebooting...");
                    // 自动重启
                    osDelay(100);
                    dummy.motorJ[ALL]->Reboot();
                    if (dummy.hand2 != nullptr)
                        dummy.hand2->Reboot();
                    osDelay(100);
                    HAL_NVIC_SystemReset();
                }
                else
                {
                    uint32_t node = strtoul(param, nullptr, 10);
                    if (node >= 1 && node <= 7)
                    {
                        if (node <= 6)
                            dummy.motorJ[node]->ApplyPositionAsHome();
                        else if (dummy.hand2 != nullptr)
                            dummy.hand2->ApplyPositionAsHome();
                        Respond(_responseChannel, "OffsetJ OK node=%lu", node);
                    }
                    else
                        Respond(_responseChannel, "OffsetJ Error: bad node=%lu", node);
                }
            }
            else
            {
                Respond(_responseChannel, "OffsetJ Error: use #OFFSET_J <1-7|all>");
            }
        }
        else if (MatchExact(_cmd, "#ACC_J"))
        {
            float S; uint32_t node;
            sscanf(_cmd, "#ACC_J %lu %f", &node, &S);
            if (node >= 1 && node <= 7){
                dummy.motorJ[node]->SetAcceleration(S);
                Respond(_responseChannel, "AccJ OK node=%lu val=%.3f", node, S);
            } else {
                Respond(_responseChannel, "AccJ Error: bad node=%lu", node);
            }
        }
        else if (MatchExact(_cmd, "#SPEED_J"))
        {
            float S; uint32_t node;
            sscanf(_cmd, "#SPEED_J %lu %f", &node, &S);
            if (node >= 1 && node <= 7){
                dummy.motorJ[node]->SetVelocityLimit(S);
                Respond(_responseChannel, "SpeedJ OK node=%lu val=%.3f", node, S);
            } else {
                Respond(_responseChannel, "SpeedJ Error: bad node=%lu", node);
            }
        }
        else if (MatchExact(_cmd, "#I_LIMIT_J"))
        {
            float I; uint32_t node;
            sscanf(_cmd, "#I_LIMIT_J %lu %f", &node, &I);
            if (node >= 1 && node <= 7){
                dummy.motorJ[node]->SetCurrentLimit(I);
                Respond(_responseChannel, "ILimitJ OK node=%lu val=%.3f", node, I);
            } else {
                Respond(_responseChannel, "ILimitJ Error: bad node=%lu", node);
            }
        }
        else if (MatchExact(_cmd, "#ONLINE"))
        {
            // #ONLINE     → 全部节点检测
            // #ONLINE <n> → 单节点检测
            uint32_t node;
            if (sscanf(_cmd, "#ONLINE %lu", &node) == 1)
            {
                if (node >= 1 && node <= 7)
                {
                    bool ol = dummy.CheckNodeOnline((uint8_t)node);
                    Respond(_responseChannel, "Online OK node=%lu %s", node, ol ? "OL" : "NL");
                }
                else
                    Respond(_responseChannel, "Online Error: use #ONLINE [1-7]");
            }
            else
            {
                bool all = dummy.CheckAllNodesOnline();
                Respond(_responseChannel, "Online OK %s", all ? "all_online" : "has_offline");
                for (int n = 1; n <= 6; n++)
                    Respond(_responseChannel, "J[%d] %s", n, dummy.motorJ[n]->isOnline ? "OL" : "NL");
                if (dummy.hand2 != nullptr)
                    Respond(_responseChannel, "HAND[7] %s", dummy.hand2->isOnline ? "OL" : "NL");
            }
        }
        else if (MatchExact(_cmd, "#GET_DCE"))
        {
            // 三种模式：
            //   #GET_DCE           → 所有节点全参数
            //   #GET_DCE <n>       → 单节点全参数
            //   #GET_DCE <n> <p>   → 单节点单参数 (kp/kv/ki/kd)
            uint32_t node;
            char param[8] = {0};
            // 辅助 lambda：根据节点号获取对应电机指针（1-6=motorJ, 7=hand2）
            auto getMotor = [&](uint32_t n) -> CtrlStepMotor* {
                if (n >= 1 && n <= 6) return dummy.motorJ[n];
                if (n == 7 && dummy.hand2 != nullptr) return (CtrlStepMotor*)dummy.hand2;
                return nullptr;
            };

            int nArgs = sscanf(_cmd, "#GET_DCE %lu %7s", &node, param);
            if (nArgs == 0 || nArgs == EOF)
            {
                // 所有节点全参数（1-7）
                for (int n = 1; n <= 7; n++)
                { CtrlStepMotor* m = getMotor(n); if (m) m->QueryDceAll(); }
                osDelay(80);
                for (int n = 1; n <= 7; n++)
                {
                    CtrlStepMotor* m = getMotor(n);
                    if (!m) continue;
                    Respond(_responseChannel, "J%d  %s:", n, m->dceQueryDone ? "OL" : "NL");
                    Respond(_responseChannel, "KP:%ld  KV:%ld  KI:%ld  KD:%ld",
                            (long)m->dceKp, (long)m->dceKv,
                            (long)m->dceKi, (long)m->dceKd);
                }
            }
            else if (nArgs == 1 && node >= 1 && node <= 7)
            {
                // 单节点全参数
                CtrlStepMotor* m = getMotor(node);
                if (m)
                {
                    m->QueryDceAll();
                    osDelay(80);
                    Respond(_responseChannel, "J%lu  %s:", node, m->dceQueryDone ? "OL" : "NL");
                    Respond(_responseChannel, "KP:%ld  KV:%ld  KI:%ld  KD:%ld",
                            (long)m->dceKp, (long)m->dceKv,
                            (long)m->dceKi, (long)m->dceKd);
                }
                else
                    Respond(_responseChannel, "GetDce Error: node %lu not available", node);
            }
            else if (nArgs == 2 && node >= 1 && node <= 7)
            {
                // 单节点单参数
                CtrlStepMotor* m = getMotor(node);
                if (!m) { Respond(_responseChannel, "GetDce Error: node %lu not available", node); }
                else
                {
                    int32_t val = 0;
                    const char* pName = nullptr;
                    if (strcmp(param, "kp") == 0 || strcmp(param, "KP") == 0)
                    { m->QueryDceKp(); osDelay(50); val = m->dceKp; pName = "KP"; }
                    else if (strcmp(param, "kv") == 0 || strcmp(param, "KV") == 0)
                    { m->QueryDceKv(); osDelay(50); val = m->dceKv; pName = "KV"; }
                    else if (strcmp(param, "ki") == 0 || strcmp(param, "KI") == 0)
                    { m->QueryDceKi(); osDelay(50); val = m->dceKi; pName = "KI"; }
                    else if (strcmp(param, "kd") == 0 || strcmp(param, "KD") == 0)
                    { m->QueryDceKd(); osDelay(50); val = m->dceKd; pName = "KD"; }
                    if (pName)
                    {
                        Respond(_responseChannel, "J%lu  %s:", node, m->dceQueryDone ? "OL" : "NL");
                        Respond(_responseChannel, "%s:%ld", pName, (long)val);
                    }
                    else
                        Respond(_responseChannel, "GetDce Error: param must be kp/kv/ki/kd");
                }
            }
            else
            {
                Respond(_responseChannel, "GetDce Error: use #GET_DCE [1-7 [kp|kv|ki|kd]]");
            }
        }
        else
            Respond(_responseChannel, "Error: unknown query");
    } else if (_cmd[0] == '>' || _cmd[0] == '@' || _cmd[0] == '&')
    {
        uint32_t freeSize = dummy.commandHandler.Push(_cmd);
        // Push 返回值：剩余空位、或拒绝码（0xFC=not_homed, 0xFD=not_enabled, 0xFE=bad_state, 0xFF=full）
        if (freeSize == 0xFC)
            Respond(_responseChannel, "Move Error: not_homed");
        else if (freeSize == 0xFD)
            Respond(_responseChannel, "Move Error: not_enabled");
        else if (freeSize == 0xFE)
            Respond(_responseChannel, "Move Error: state=%s", StateName((int)dummy.robotState));
        else if (freeSize == 0xFF)
            Respond(_responseChannel, "Move Error: queue_full");
        else
            Respond(_responseChannel, "Move OK Q:%d", freeSize);
    }
    else
    {
        Respond(_responseChannel, "Error: unknown command, use '>'/'@'/'&'/'!'/'#' prefix");
    }

/*---------------------------- ↑ Add Your CMDs Here ↑ -----------------------------*/
}


void OnUart4AsciiCmd(const char* _cmd, size_t _len, StreamSink &_responseChannel)
{
    /*---------------------------- ↓ Add Your CMDs Here ↓ -----------------------------*/
    if (_cmd[0] == '!')
    {
        // 最高优先级：急停 / 暂停 / 恢复（精确前缀匹配）
        if (MatchExact(_cmd, "!STOP"))
        {
            dummy.commandHandler.EmergencyStop();
            Respond(_responseChannel, "Stop OK");
            Respond(_responseChannel, "STATE:%s QUEUE:%lu",
                    StateName((int)dummy.robotState),
                    (unsigned long)dummy.commandHandler.GetSpace());
            return;
        }
        if (MatchExact(_cmd, "!PAUSE"))
        {
            if (!dummy.commandHandler.Pause())
            {
                Respond(_responseChannel, "Pause Error: state=%s",
                        StateName((int)dummy.robotState));
            }
            else
            {
                Respond(_responseChannel, "Pause OK");
                Respond(_responseChannel, "STATE:%s QUEUE:%lu",
                        StateName((int)dummy.robotState),
                        (unsigned long)dummy.commandHandler.GetSpace());
            }
            return;
        }
        if (MatchExact(_cmd, "!RESUME"))
        {
            dummy.commandHandler.Resume();
            Respond(_responseChannel, "Resume OK");
            Respond(_responseChannel, "STATE:%s QUEUE:%lu",
                    StateName((int)dummy.robotState),
                    (unsigned long)dummy.commandHandler.GetSpace());
            return;
        }
        if (MatchExact(_cmd, "!CLOSE"))
        {
            if (dummy.robotState != DummyRobot::STATE_NORMAL)
            {
                Respond(_responseChannel, "Close Error: state=%s",
                        StateName((int)dummy.robotState));
            }
            else
            {
                dummy.PostSysCmd(DummyRobot::SYSCMD_SHUTDOWN, _responseChannel.channelType);
            }
            return;
        }

        if (false)
        {
        } else if (MatchExact(_cmd, "!START"))
        {
            bool allOk = dummy.CheckAllNodesOnline();
            if (!allOk)
            {
                char buf[80]; buf[0] = '\0';
                size_t off = 0;
                for (int n = 1; n <= 6; n++)
                    if (!dummy.motorJ[n]->isOnline)
                        off += snprintf(buf + off, sizeof(buf) - off, "J[%d] ", n);
                if (dummy.hand2 != nullptr && !dummy.hand2->isOnline)
                    off += snprintf(buf + off, sizeof(buf) - off, "HAND[7] ");
                Respond(_responseChannel, "Start Error: offline=%s", buf);
            }
            else
            {
                dummy.PostSysCmd(DummyRobot::SYSCMD_STARTUP, _responseChannel.channelType);
            }
        } else if (MatchExact(_cmd, "!HOME"))
        {
            if (!dummy.IsEnabled())
            {
                Respond(_responseChannel, "Home Error: not_enabled");
            }
            else
            {
                dummy.PostSysCmd(DummyRobot::SYSCMD_HOME, _responseChannel.channelType);
            }
        } else if (MatchExact(_cmd, "!CALIBRATION"))
        {
            dummy.CalibrateHomeOffset();
            Respond(_responseChannel, "Calibration OK");
        } else if (MatchExact(_cmd, "!RESET"))
        {
            if (!dummy.IsEnabled())
            {
                Respond(_responseChannel, "Reset Error: not_enabled");
            }
            else
            {
                dummy.PostSysCmd(DummyRobot::SYSCMD_RESET, _responseChannel.channelType);
            }
        }
        // 手爪控制（hand2 / StepHand）—— 失能时拒绝运动命令
        else if (MatchExact(_cmd, "!HD_O"))
        {
            if (!dummy.hand2->isEnabled())
            {
                Respond(_responseChannel, "HD_O Error: hand disabled (use !HD_EN or !START)");
            }
            else
            {
                dummy.hand2->SetAngleWithCurrentLimit(-1);
                dummy.OnHandActionRGBHook(true);
                Respond(_responseChannel, "HD_O OK");
            }
        }
        else if (MatchExact(_cmd, "!HD_C"))
        {
            if (!dummy.hand2->isEnabled())
            {
                Respond(_responseChannel, "HD_C Error: hand disabled (use !HD_EN or !START)");
            }
            else
            {
                dummy.hand2->SetAngleWithCurrentLimit(1);
                dummy.OnHandActionRGBHook(false);
                Respond(_responseChannel, "HD_C OK");
            }
        }
        // RGB 控制命令
        else if (MatchExact(_cmd, "!RGB_ON"))
        {
            dummy.SetRGBEnable(true);
            Respond(_responseChannel, "RGB_ON OK");
        }
        else if (MatchExact(_cmd, "!RGB_OFF"))
        {
            dummy.SetRGBEnable(false);
            Respond(_responseChannel, "RGB_OFF OK");
        }
        else if (MatchExact(_cmd, "!RGB_MODE"))
        {
            uint32_t mode;
            if (sscanf(_cmd, "!RGB_MODE %lu", &mode) == 1)
            {
                if (mode <= 11)
                {
                    dummy.SetRGBMode(mode);
                    Respond(_responseChannel, "RGB_MODE OK mode=%lu", mode);
                }
                else
                {
                    Respond(_responseChannel, "RGB_MODE Error: mode must be 0-11");
                }
            }
            else
            {
                Respond(_responseChannel, "RGB_MODE Error: use !RGB_MODE <0-11>");
            }
        }
        else if (MatchExact(_cmd, "!RGB_CUSTOM"))
        {
            uint32_t r, g, b;
            if (sscanf(_cmd, "!RGB_CUSTOM %lu %lu %lu", &r, &g, &b) == 3 && r <= 255 && g <= 255 && b <= 255)
            {
                rgb.customR = (uint8_t)r; rgb.customG = (uint8_t)g; rgb.customB = (uint8_t)b;
                dummy.SetRGBMode(8);  // 切到自定义
                Respond(_responseChannel, "RGB_CUSTOM OK r=%lu g=%lu b=%lu", r, g, b);
            }
            else
            {
                Respond(_responseChannel, "RGB_CUSTOM Error: use !RGB_CUSTOM <r> <g> <b> (0-255)");
            }
        }
        else if (MatchExact(_cmd, "!HD_EN"))
        {
            dummy.hand2->SetEnable(true);
            dummy.hand2->UpdateAngle();
            Respond(_responseChannel, "HD_EN OK enabled=%lu angle=%.2f",
                    (unsigned long)dummy.hand2->isEnabled(), dummy.hand2->angle);
        }
        else if (MatchExact(_cmd, "!HD_DIS"))
        {
            dummy.hand2->SetEnable(false);
            dummy.hand2->UpdateAngle();
            Respond(_responseChannel, "HD_DIS OK enabled=%lu angle=%.2f",
                    (unsigned long)dummy.hand2->isEnabled(), dummy.hand2->angle);
        }
        else if (MatchExact(_cmd, "!HD_ZERO"))
        {
            dummy.hand2->HandCalibration();
            dummy.hand2->UpdateAngle();
            Respond(_responseChannel, "HD_ZERO OK MIN:%.2f MAX:%.2f",
                    dummy.hand2->ClosedAngle, dummy.hand2->OpenedAngle);
        }
        else if (MatchExact(_cmd, "!HD_POS"))
        {
            uint32_t pos;
            if (!dummy.hand2->isEnabled())
            {
                Respond(_responseChannel, "HD_POS Error: hand disabled (use !HD_EN or !START)");
            }
            else if (sscanf(_cmd, "!HD_POS %lu", &pos) == 1)
            {
                if (pos <= 100)
                {
                    dummy.hand2->SetPercentToAngle(static_cast<float>(pos));
                    dummy.hand2->UpdateAngle();
                    Respond(_responseChannel, "HD_POS OK pos=%lu angle=%.2f",
                            (unsigned long)pos, dummy.hand2->angle);
                }
                else
                {
                    Respond(_responseChannel, "HD_POS Error: out of range (0-100)");
                }
            }
            else
            {
                Respond(_responseChannel, "HD_POS Error: use !HD_POS <0-100>");
            }
        }
        else if (MatchExact(_cmd, "!HD_I"))
        {
            float cu;
            if (sscanf(_cmd, "!HD_I %f", &cu) == 1)
            {
                if (cu > 0 && cu <= 2)
                {
                    dummy.hand2->current = cu;
                    Respond(_responseChannel, "HD_I OK current=%.2f", cu);
                }
                else
                {
                    Respond(_responseChannel, "HD_I Error: out of range (0-2.0)");
                }
            }
            else
            {
                Respond(_responseChannel, "HD_I Error: use !HD_I <0-2.0>");
            }
        }
        else
        {
            Respond(_responseChannel, "Error: unknown command");
        }
    } else if (_cmd[0] == '#')
    {
        if (MatchExact(_cmd, "#GETJPOS"))
        {
            int handState = 0;
            if (dummy.hand2 != nullptr)
            {
                dummy.hand2->UpdateAngle();
                float mid = (dummy.hand2->OpenedAngle + dummy.hand2->ClosedAngle) / 2.0f;
                handState = (dummy.hand2->angle < mid) ? 1 : 0;
            }
            Respond(_responseChannel, "GetJPos OK %.2f %.2f %.2f %.2f %.2f %.2f H:%d",
                    dummy.currentJoints.a[0], dummy.currentJoints.a[1],
                    dummy.currentJoints.a[2], dummy.currentJoints.a[3],
                    dummy.currentJoints.a[4], dummy.currentJoints.a[5],
                    handState);
        } else if (MatchExact(_cmd, "#GETLPOS"))
        {
            dummy.UpdateJointPose6D();
            int handState = 0;
            if (dummy.hand2 != nullptr)
            {
                dummy.hand2->UpdateAngle();
                float mid = (dummy.hand2->OpenedAngle + dummy.hand2->ClosedAngle) / 2.0f;
                handState = (dummy.hand2->angle < mid) ? 1 : 0;
            }
            Respond(_responseChannel, "GetLPos OK %.2f %.2f %.2f %.2f %.2f %.2f H:%d",
                    dummy.currentPose6D.X, dummy.currentPose6D.Y,
                    dummy.currentPose6D.Z, dummy.currentPose6D.A,
                    dummy.currentPose6D.B, dummy.currentPose6D.C,
                    handState);
        } else if (MatchExact(_cmd, "#GETSPEED"))
        {
            Respond(_responseChannel, "GetSpeed OK SPEED:%.2f", dummy.GetJointSpeed());
        } else if (MatchExact(_cmd, "#CMDMODE"))
        {
            uint32_t mode;
            sscanf(_cmd, "#CMDMODE %lu", &mode);
            dummy.SetCommandMode(mode);
            Respond(_responseChannel, "CmdMode OK mode=%lu", mode);
        } else if (MatchExact(_cmd, "#REBOOT"))
        {
            uint32_t node;
            if (sscanf(_cmd, "#REBOOT %lu", &node) == 1)
            {
                if (node == 0)
                {
                    Respond(_responseChannel, "Reboot OK ref (reset in 100ms)");
                    osDelay(100);
                    HAL_NVIC_SystemReset();
                }
                else if (node >= 1 && node <= 6)
                {
                    dummy.motorJ[node]->Reboot();
                    osDelay(20);
                    Respond(_responseChannel, "Reboot OK motor=%lu", node);
                }
                else if (node == 7)
                {
                    if (dummy.hand2 != nullptr)
                    {
                        dummy.hand2->Reboot();
                        osDelay(20);
                        Respond(_responseChannel, "Reboot OK hand");
                    }
                    else
                        Respond(_responseChannel, "Reboot Error: hand not available");
                }
                else
                    Respond(_responseChannel, "Reboot Error: bad node=%lu", node);
            }
            else
            {
                dummy.motorJ[ALL]->Reboot();
                if (dummy.hand2 != nullptr)
                    dummy.hand2->Reboot();
                Respond(_responseChannel, "Reboot OK all (reset in 100ms)");
                osDelay(100);
                HAL_NVIC_SystemReset();
            }
        } else if (MatchExact(_cmd, "#OFFSET_J"))
        {
            char param[16] = {0};
            if (sscanf(_cmd, "#OFFSET_J %15s", param) == 1)
            {
                if (strcmp(param, "all") == 0 || strcmp(param, "ALL") == 0)
                {
                    for (int n = 1; n <= 6; n++)
                    {
                        dummy.motorJ[n]->ApplyPositionAsHome();
                        osDelay(10);
                    }
                    if (dummy.hand2 != nullptr)
                    {
                        dummy.hand2->ApplyPositionAsHome();
                        osDelay(10);
                    }
                    Respond(_responseChannel, "OffsetJ OK all (reset in 100ms)");
                    osDelay(100);
                    dummy.motorJ[ALL]->Reboot();
                    if (dummy.hand2 != nullptr)
                        dummy.hand2->Reboot();
                    osDelay(100);
                    HAL_NVIC_SystemReset();
                }
                else
                {
                    uint32_t node = strtoul(param, nullptr, 10);
                    if (node >= 1 && node <= 7)
                    {
                        if (node <= 6)
                            dummy.motorJ[node]->ApplyPositionAsHome();
                        else if (dummy.hand2 != nullptr)
                            dummy.hand2->ApplyPositionAsHome();
                        Respond(_responseChannel, "OffsetJ OK node=%lu", node);
                    }
                    else
                        Respond(_responseChannel, "OffsetJ Error: bad node=%lu", node);
                }
            }
            else
                Respond(_responseChannel, "OffsetJ Error: use #OFFSET_J <1-7|all>");
        } else if (MatchExact(_cmd, "#ONLINE"))
        {
            uint32_t node;
            if (sscanf(_cmd, "#ONLINE %lu", &node) == 1)
            {
                if (node >= 1 && node <= 7)
                {
                    bool ol = dummy.CheckNodeOnline((uint8_t)node);
                    Respond(_responseChannel, "Online OK node=%lu %s", node, ol ? "OL" : "NL");
                }
                else
                    Respond(_responseChannel, "Online Error: use #ONLINE [1-7]");
            }
            else
            {
                bool all = dummy.CheckAllNodesOnline();
                Respond(_responseChannel, "Online OK %s", all ? "all_online" : "has_offline");
                for (int n = 1; n <= 6; n++)
                    Respond(_responseChannel, "J[%d] %s", n, dummy.motorJ[n]->isOnline ? "OL" : "NL");
                if (dummy.hand2 != nullptr)
                    Respond(_responseChannel, "HAND[7] %s", dummy.hand2->isOnline ? "OL" : "NL");
            }
        } else if (MatchExact(_cmd, "#GET_DCE"))
        {
            uint32_t node;
            char param[8] = {0};
            auto getMotor = [&](uint32_t n) -> CtrlStepMotor* {
                if (n >= 1 && n <= 6) return dummy.motorJ[n];
                if (n == 7 && dummy.hand2 != nullptr) return (CtrlStepMotor*)dummy.hand2;
                return nullptr;
            };
            int nArgs = sscanf(_cmd, "#GET_DCE %lu %7s", &node, param);
            if (nArgs == 0 || nArgs == EOF)
            {
                for (int n = 1; n <= 7; n++)
                { CtrlStepMotor* m = getMotor(n); if (m) m->QueryDceAll(); }
                osDelay(80);
                for (int n = 1; n <= 7; n++)
                {
                    CtrlStepMotor* m = getMotor(n);
                    if (!m) continue;
                    Respond(_responseChannel, "J%d  %s:", n, m->dceQueryDone ? "OL" : "NL");
                    Respond(_responseChannel, "KP:%ld  KV:%ld  KI:%ld  KD:%ld",
                            (long)m->dceKp, (long)m->dceKv,
                            (long)m->dceKi, (long)m->dceKd);
                }
            }
            else if (nArgs == 1 && node >= 1 && node <= 7)
            {
                CtrlStepMotor* m = getMotor(node);
                if (m)
                {
                    m->QueryDceAll(); osDelay(80);
                    Respond(_responseChannel, "J%lu  %s:", node, m->dceQueryDone ? "OL" : "NL");
                    Respond(_responseChannel, "KP:%ld  KV:%ld  KI:%ld  KD:%ld",
                            (long)m->dceKp, (long)m->dceKv,
                            (long)m->dceKi, (long)m->dceKd);
                }
                else
                    Respond(_responseChannel, "GetDce Error: node %lu not available", node);
            }
            else if (nArgs == 2 && node >= 1 && node <= 7)
            {
                CtrlStepMotor* m = getMotor(node);
                if (!m) { Respond(_responseChannel, "GetDce Error: node %lu not available", node); }
                else
                {
                    int32_t val = 0; const char* pName = nullptr;
                    if (strcmp(param,"kp")==0||strcmp(param,"KP")==0)
                    { m->QueryDceKp(); osDelay(50); val=m->dceKp; pName="KP"; }
                    else if (strcmp(param,"kv")==0||strcmp(param,"KV")==0)
                    { m->QueryDceKv(); osDelay(50); val=m->dceKv; pName="KV"; }
                    else if (strcmp(param,"ki")==0||strcmp(param,"KI")==0)
                    { m->QueryDceKi(); osDelay(50); val=m->dceKi; pName="KI"; }
                    else if (strcmp(param,"kd")==0||strcmp(param,"KD")==0)
                    { m->QueryDceKd(); osDelay(50); val=m->dceKd; pName="KD"; }
                    if (pName)
                    {
                        Respond(_responseChannel, "J%lu  %s:", node, m->dceQueryDone ? "OL" : "NL");
                        Respond(_responseChannel, "%s:%ld", pName, (long)val);
                    }
                    else Respond(_responseChannel, "GetDce Error: param must be kp/kv/ki/kd");
                }
            }
            else
                Respond(_responseChannel, "GetDce Error: use #GET_DCE [1-7 [kp|kv|ki|kd]]");
        }
        else if (MatchExact(_cmd, "#SET_DCE_KV"))
        {
            uint32_t kp; uint32_t node;
            sscanf(_cmd, "#SET_DCE_KV %lu %lu", &node, &kp);
            if (node >= 1 && node <= 6) {
                dummy.motorJ[node]->SetDceKv(kp);
                Respond(_responseChannel, "SetDceKv OK node=%lu val=%lu", node, kp);
            } else {
                Respond(_responseChannel, "SetDceKv Error: bad node=%lu", node);
            }
        }
        else if (MatchExact(_cmd, "#SET_DCE_KP"))
        {
            uint32_t kp; uint32_t node;
            sscanf(_cmd, "#SET_DCE_KP %lu %lu", &node, &kp);
            if (node >= 1 && node <= 6) {
                dummy.motorJ[node]->SetDceKp(kp);
                Respond(_responseChannel, "SetDceKp OK node=%lu val=%lu", node, kp);
            } else {
                Respond(_responseChannel, "SetDceKp Error: bad node=%lu", node);
            }
        }
        else if (MatchExact(_cmd, "#SET_DCE_KI"))
        {
            uint32_t kp; uint32_t node;
            sscanf(_cmd, "#SET_DCE_KI %lu %lu", &node, &kp);
            if (node >= 1 && node <= 6) {
                dummy.motorJ[node]->SetDceKi(kp);
                Respond(_responseChannel, "SetDceKi OK node=%lu val=%lu", node, kp);
            } else {
                Respond(_responseChannel, "SetDceKi Error: bad node=%lu", node);
            }
        }
        else if (MatchExact(_cmd, "#SET_DCE_KD"))
        {
            uint32_t kp; uint32_t node;
            sscanf(_cmd, "#SET_DCE_KD %lu %lu", &node, &kp);
            if (node >= 1 && node <= 6) {
                dummy.motorJ[node]->SetDceKd(kp);
                Respond(_responseChannel, "SetDceKd OK node=%lu val=%lu", node, kp);
            } else {
                Respond(_responseChannel, "SetDceKd Error: bad node=%lu", node);
            }
        }
        else if (MatchExact(_cmd, "#ACC_J"))
        {
            float S; uint32_t node;
            sscanf(_cmd, "#ACC_J %lu %f", &node, &S);
            if (node >= 1 && node <= 7) {
                dummy.motorJ[node]->SetAcceleration(S);
                Respond(_responseChannel, "AccJ OK node=%lu val=%.3f", node, S);
            } else {
                Respond(_responseChannel, "AccJ Error: bad node=%lu", node);
            }
        }
        else if (MatchExact(_cmd, "#SPEED_J"))
        {
            float S; uint32_t node;
            sscanf(_cmd, "#SPEED_J %lu %f", &node, &S);
            if (node >= 1 && node <= 7) {
                dummy.motorJ[node]->SetVelocityLimit(S);
                Respond(_responseChannel, "SpeedJ OK node=%lu val=%.3f", node, S);
            } else {
                Respond(_responseChannel, "SpeedJ Error: bad node=%lu", node);
            }
        }
        else if (MatchExact(_cmd, "#I_LIMIT_J"))
        {
            float I; uint32_t node;
            sscanf(_cmd, "#I_LIMIT_J %lu %f", &node, &I);
            if (node >= 1 && node <= 7) {
                dummy.motorJ[node]->SetCurrentLimit(I);
                Respond(_responseChannel, "ILimitJ OK node=%lu val=%.3f", node, I);
            } else {
                Respond(_responseChannel, "ILimitJ Error: bad node=%lu", node);
            }
        }
        else
            Respond(_responseChannel, "Error: unknown query");
    } else if (_cmd[0] == '!' && MatchExact(_cmd, "!RGB_MODE"))
    {
        uint32_t mode;
        if (sscanf(_cmd, "!RGB_MODE %lu", &mode) == 1 && mode <= 11)
        {
            dummy.SetRGBMode(mode);
            Respond(_responseChannel, "RGB_MODE OK mode=%lu", mode);
        }
        else
            Respond(_responseChannel, "RGB_MODE Error: use !RGB_MODE <0-11>");
    } else if (_cmd[0] == '>' || _cmd[0] == '@' || _cmd[0] == '&')
    {
        uint32_t freeSize = dummy.commandHandler.Push(_cmd);
        if (freeSize == 0xFC)
            Respond(_responseChannel, "Move Error: not_homed");
        else if (freeSize == 0xFD)
            Respond(_responseChannel, "Move Error: not_enabled");
        else if (freeSize == 0xFE)
            Respond(_responseChannel, "Move Error: state=%s", StateName((int)dummy.robotState));
        else if (freeSize == 0xFF)
            Respond(_responseChannel, "Move Error: queue_full");
        else
            Respond(_responseChannel, "Move OK Q:%d", freeSize);
    }
    else
    {
        Respond(_responseChannel, "Error: unknown command, use '>'/'@'/'&'/'!'/'#' prefix");
    }
/*---------------------------- ↑ Add Your CMDs Here ↑ -----------------------------*/
}


void OnUart5AsciiCmd(const char* _cmd, size_t _len, StreamSink &_responseChannel)
{
    /*---------------------------- ↓ Add Your CMDs Here ↓ -----------------------------*/

/*---------------------------- ↑ Add Your CMDs Here ↑ -----------------------------*/
}
