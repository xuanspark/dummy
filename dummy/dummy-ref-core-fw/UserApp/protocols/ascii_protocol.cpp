#include "common_inc.h"
#include <cstring>

extern DummyRobot dummy;

// 精确前缀匹配：匹配 _cmd 以 _kw 开头且后一个字符为结束符/空白
// 避免 "STOP" 误中 "!STOPX" 之类。
static inline bool MatchExact(const char* _cmd, const char* _kw)
{
    size_t n = std::strlen(_kw);
    if (std::strncmp(_cmd, _kw, n) != 0) return false;
    char c = _cmd[n];
    return c == '\0' || c == '\r' || c == '\n' || c == ' ' || c == '\t';
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
            Respond(_responseChannel,
                    "ESTOP|J:%.2f,%.2f,%.2f,%.2f,%.2f,%.2f|STATE:%d|QUEUE:%lu",
                    dummy.currentJoints.a[0], dummy.currentJoints.a[1],
                    dummy.currentJoints.a[2], dummy.currentJoints.a[3],
                    dummy.currentJoints.a[4], dummy.currentJoints.a[5],
                    (int)dummy.robotState,
                    (unsigned long)dummy.commandHandler.GetSpace());
            return;
        }
        if (MatchExact(_cmd, "!PAUSE"))
        {
            dummy.commandHandler.Pause();
            Respond(_responseChannel,
                    "PAUSED|J:%.2f,%.2f,%.2f,%.2f,%.2f,%.2f|STATE:%d|QUEUE:%lu",
                    dummy.currentJoints.a[0], dummy.currentJoints.a[1],
                    dummy.currentJoints.a[2], dummy.currentJoints.a[3],
                    dummy.currentJoints.a[4], dummy.currentJoints.a[5],
                    (int)dummy.robotState,
                    (unsigned long)dummy.commandHandler.GetSpace());
            return;
        }
        if (MatchExact(_cmd, "!RESUME"))
        {
            int oldState = (int)dummy.robotState;
            dummy.commandHandler.Resume();
            if (oldState != DummyRobot::STATE_NORMAL)
                Respond(_responseChannel, "RESUMED|FROM_STATE:%d|SPEED:%.1f|QUEUE:%lu",
                        oldState, dummy.GetJointSpeed(),
                        (unsigned long)dummy.commandHandler.GetSpace());
            else
                Respond(_responseChannel, "ALREADY_NORMAL|QUEUE:%lu",
                        (unsigned long)dummy.commandHandler.GetSpace());
            return;
        }
        // !CLOSE 必须在 find("CLOSE") 手爪分支之前精确匹配，避免被误入手爪闭合分支
        if (MatchExact(_cmd, "!CLOSE"))
        {
            if (dummy.robotState != DummyRobot::STATE_NORMAL)
            {
                Respond(_responseChannel, "rejected: robot in state %d", (int)dummy.robotState);
            }
            else
            {
                dummy.Shutdown();
                Respond(_responseChannel, "Closed ok");
            }
            return;
        }

        // 以下均精确匹配（MatchExact 允许后跟空格/\r/\n/\0/\t）
        if (false)
        {
            // placeholder to keep the original else-if chain below intact
        } else if (MatchExact(_cmd, "!START"))
        {
            // 使能机械臂 + 手爪，回零点姿态（REST_POSE）
            dummy.Startup();
            Respond(_responseChannel, "Started ok");
        } else if (MatchExact(_cmd, "!HOME"))
        {
            // !HOME 必须已使能（不自动使能以确保安全）
            if (!dummy.IsEnabled())
            {
                Respond(_responseChannel, "rejected: not_enabled, send !START first");
            }
            else
            {
                dummy.Homing();
                Respond(_responseChannel, "Started ok");
            }
        } else if (MatchExact(_cmd, "!CALIBRATION"))
        {
            dummy.CalibrateHomeOffset();
            Respond(_responseChannel, "calibration ok");
        } else if (MatchExact(_cmd, "!RESET"))
        {
            // !RESET 仅回零点姿态，不失能（要失能请用 !CLOSE）；需已使能
            if (!dummy.IsEnabled())
            {
                Respond(_responseChannel, "rejected: not_enabled, send !START first");
            }
            else
            {
                dummy.Resting();
                Respond(_responseChannel, "Started ok");
            }
        }
        // 手爪控制（hand2 / StepHand）
        else if (MatchExact(_cmd, "!HD_O"))
        {
            dummy.hand2->SetAngleWithCurrentLimit(-1);
            dummy.OnHandActionRGBHook(true);  // RGB 联动 hook（预留）
            Respond(_responseChannel, "ok hand open");
        }
        else if (MatchExact(_cmd, "!HD_C"))
        {
            dummy.hand2->SetAngleWithCurrentLimit(1);
            dummy.OnHandActionRGBHook(false); // RGB 联动 hook（预留）
            Respond(_responseChannel, "ok hand close");
        }
        // RGB 控制命令
        else if (MatchExact(_cmd, "!RGB_ON"))
        {
            dummy.SetRGBEnable(true);
            Respond(_responseChannel, "RGB enabled");
        }
        else if (MatchExact(_cmd, "!RGB_OFF"))
        {
            dummy.SetRGBEnable(false);
            Respond(_responseChannel, "RGB disabled");
        }
        else if (MatchExact(_cmd, "!RGB_MODE"))
        {
            uint32_t mode;
            if (sscanf(_cmd, "!RGB_MODE %lu", &mode) == 1)
            {
                if (mode <= 6)
                {
                    dummy.SetRGBMode(mode);
                    Respond(_responseChannel, "RGB mode set to %lu", mode);
                }
                else
                {
                    Respond(_responseChannel, "error: mode must be 0-6");
                }
            }
            else
            {
                Respond(_responseChannel, "error: invalid format. Use !RGB_MODE <0-6>");
            }
        }
        else if (MatchExact(_cmd, "!HD_EN"))
        {
            dummy.hand2->SetEnable(true);
            Respond(_responseChannel, "ok hand enable");
            Respond(_responseChannel, "ok hand enable/disable is %lu real_angle:%f", dummy.hand2->isEnabled(),dummy.hand2->angle);
        }
        else if (MatchExact(_cmd, "!HD_DIS"))
        {
            dummy.hand2->SetEnable(false);
            Respond(_responseChannel, "ok hand disable");
            dummy.hand2->UpdateAngle();
            Respond(_responseChannel, "ok hand enable/disable is %lu real_angle:%f", dummy.hand2->isEnabled(),dummy.hand2->angle);
        }
        else if (MatchExact(_cmd, "!HD_ZERO"))
        {
            Respond(_responseChannel, "hand offset start");
            dummy.hand2->HandCalibration();
            dummy.hand2->UpdateAngle();
            Respond(_responseChannel, "ok hand offset MIN:%f max:%f ",dummy.hand2->ClosedAngle, dummy.hand2->OpenedAngle);
        }
        else if (MatchExact(_cmd, "!HD_POS"))
        {
            uint32_t pos;
            if (sscanf(_cmd, "!HD_POS %lu", &pos) == 1)  // Check if parsing succeeded
            {
                if (pos <= 100 && pos >= 0)  // Removed redundant pos>=0 check since uint32_t is always >=0
                {
                    dummy.hand2->SetPercentToAngle(static_cast<float>(pos));

                    dummy.hand2->UpdateAngle();
                    Respond(_responseChannel, "ok hand position %lu and real_angle:%f", pos,dummy.hand2->angle);

                }
                else
                {
                    Respond(_responseChannel, "error hand position %lu - Value exceeds maximum (100)", pos);
                }
            }
            else
            {
                Respond(_responseChannel, "error hand position - Invalid format. Use !HD_POS <0-100>");
            }
        }
        else if (MatchExact(_cmd, "!HD_I"))
        {
            float cu;
            if (sscanf(_cmd, "!HD_I %f", &cu) == 1)  // Check if parsing succeeded
            {

                if (cu <= 2 && cu>0)  // Removed redundant pos>=0 check since uint32_t is always >=0
                {
                    dummy.hand2->current=static_cast<float>(cu);

                    Respond(_responseChannel, "ok hand current %f", cu);
                }
                else
                {
                    Respond(_responseChannel, "error hand current %f - Value exceeds maximum (2.0)", cu);
                }
            }
            else
            {
                Respond(_responseChannel, "error hand current - Invalid format. Use #value <0-2.0> ");
            }
        }

    } else if (_cmd[0] == '#')
    {
        if (MatchExact(_cmd, "#GETJPOS"))
        {
            Respond(_responseChannel, "ok %.2f %.2f %.2f %.2f %.2f %.2f",
                    dummy.currentJoints.a[0], dummy.currentJoints.a[1],
                    dummy.currentJoints.a[2], dummy.currentJoints.a[3],
                    dummy.currentJoints.a[4], dummy.currentJoints.a[5]);
        } else if (MatchExact(_cmd, "#GETLPOS"))
        {
            dummy.UpdateJointPose6D();
            Respond(_responseChannel, "ok %.2f %.2f %.2f %.2f %.2f %.2f",
                    dummy.currentPose6D.X, dummy.currentPose6D.Y,
                    dummy.currentPose6D.Z, dummy.currentPose6D.A,
                    dummy.currentPose6D.B, dummy.currentPose6D.C);
        }
        else if (MatchExact(_cmd, "#GETSPEED"))
        {
            Respond(_responseChannel, "ok SPEED:%.2f", dummy.GetJointSpeed());
        }
        else if (MatchExact(_cmd, "#SET_DCE_KV"))
        {
            uint32_t kp;
            uint32_t node;
            sscanf(_cmd, "#SET_DCE_KV %lu %lu", &node, &kp);
            if (node >= 1 & node <= 6){
                dummy.motorJ[node]->SetDceKv(kp);
                Respond(_responseChannel, "ok SET MOTOR [%lu] DCE_KV [%lu]", node, kp);
            }
            else {
                Respond(_responseChannel, "error SET MOTOR [%lu] DCE_KV [%lu] is wrong", node, kp);
            }
        }
        else if (MatchExact(_cmd, "#SET_DCE_KP"))
        {
            uint32_t kp;
            uint32_t node;
            sscanf(_cmd, "#SET_DCE_KP %lu %lu", &node, &kp);
            if (node >= 1 & node <= 6){
                dummy.motorJ[node]->SetDceKp(kp);
                Respond(_responseChannel, "ok SET MOTOR [%lu] DCE_KP [%lu]", node, kp);
            }
            else {
                Respond(_responseChannel, "error SET MOTOR [%lu] DCE_KP [%lu] is wrong", node, kp);
            }
        } else if (MatchExact(_cmd, "#SET_DCE_KI"))
        {
            uint32_t kp;
            uint32_t node;
            sscanf(_cmd, "#SET_DCE_KI %lu %lu", &node, &kp);
            if (node >= 1 & node <= 6){
                dummy.motorJ[node]->SetDceKi(kp);
                Respond(_responseChannel, "ok SET MOTOR [%lu] DCE_KI [%lu]", node, kp);
            }
            else {
                Respond(_responseChannel, "error SET MOTOR [%lu] DCE_KI [%lu] is wrong", node, kp);
            }
        } else if (MatchExact(_cmd, "#SET_DCE_KD"))
        {
            uint32_t kp;
            uint32_t node;
            sscanf(_cmd, "#SET_DCE_KD %lu %lu", &node, &kp);
            if (node >= 1 & node <= 6){
                dummy.motorJ[node]->SetDceKd(kp);
                Respond(_responseChannel, "ok SET MOTOR [%lu] DCE_KD [%lu]", node, kp);
            }
            else {
                Respond(_responseChannel, "error SET MOTOR [%lu] DCE_KD [%lu] is wrong", node, kp);
            }
        } else if (MatchExact(_cmd, "#REBOOT"))
        {
            uint32_t node;
            sscanf(_cmd, "#REBOOT %lu", &node);
            if (node >= 1 & node <= 6){
                dummy.motorJ[node]->Reboot();
                Respond(_responseChannel, "ok REBOOT MOTOR [%lu]", node);
            }
            else {
                Respond(_responseChannel, "error REBOOT MOTOR [%lu] is wrong", node);
            }
        } else if (MatchExact(_cmd, "#CMDMODE"))
        {
            uint32_t mode;
            sscanf(_cmd, "#CMDMODE %lu", &mode);
            dummy.SetCommandMode(mode);
            Respond(_responseChannel, "ok Set command mode to [%lu]", mode);
        }
        else if (MatchExact(_cmd, "#OFFSET_J"))
        {
            uint32_t node;
            sscanf(_cmd, "#OFFSET_J %lu", &node);
            if (node >= 1 && node <= 7){
                dummy.motorJ[node]->ApplyPositionAsHome();
                Respond(_responseChannel, "ok HOMEOFFSET MOTOR [%lu]", node);
            }
            else {
                Respond(_responseChannel, "error HOMEOFFSET MOTOR [%lu] is wrong", node);
            }
        }
        else if (MatchExact(_cmd, "#ACC_J"))
        {
            float S;
            uint32_t node;
            sscanf(_cmd, "#ACC_J %lu %f", &node, &S);
            if (node >= 1 & node <= 7){
                dummy.motorJ[node]->SetAcceleration(S);
                Respond(_responseChannel, "ok SET MOTOR [%lu] ACCELERATION [%f]", node, S);
            }
            else {
                Respond(_responseChannel, "error SET MOTOR [%lu] ACCELERATION [%f] is wrong", node, S);
            }
        }
        else if (MatchExact(_cmd, "#SPEED_J"))
        {
            float S;
            uint32_t node;
            sscanf(_cmd, "#SPEED_J %lu %f", &node, &S);
            if (node >= 1 & node <= 7){
                dummy.motorJ[node]->SetVelocityLimit(S);
                Respond(_responseChannel, "ok SET MOTOR [%lu] SPEED [%f]", node, S);
            }
            else {
                Respond(_responseChannel, "error SET MOTOR [%lu] SPEED [%f] is wrong", node, S);
            }
        }
        else if (MatchExact(_cmd, "#I_LIMIT_J"))
        {
            float I;
            uint32_t node;
            sscanf(_cmd, "#I_LIMIT_J %lu %f", &node, &I);
            if (node >= 1 & node <= 7){
                dummy.motorJ[node]->SetCurrentLimit(I);
                Respond(_responseChannel, "ok SET MOTOR [%lu] CURRENT_LIMIT [%f]", node, I);
            }
            else {
                Respond(_responseChannel, "error SET MOTOR [%lu] CURRENT_LIMIT [%f] is wrong", node, I);
            }
        }
        else
            Respond(_responseChannel, "ok");
    } else if (_cmd[0] == '>' || _cmd[0] == '@' || _cmd[0] == '&')
    {
        uint32_t freeSize = dummy.commandHandler.Push(_cmd);
        // Push 返回值：剩余空位、或拒绝码（0xFC=not_homed, 0xFD=not_enabled, 0xFE=bad_state, 0xFF=full）
        if (freeSize == 0xFC)
            Respond(_responseChannel, "rejected: not_homed, send !HOME first");
        else if (freeSize == 0xFD)
            Respond(_responseChannel, "rejected: not_enabled, send !START first");
        else if (freeSize == 0xFE)
            Respond(_responseChannel, "rejected: state=%d", (int)dummy.robotState);
        else if (freeSize == 0xFF)
            Respond(_responseChannel, "rejected: queue_full");
        else
            Respond(_responseChannel, "%d", freeSize);
    }

/*---------------------------- ↑ Add Your CMDs Here ↑ -----------------------------*/
}


void OnUart4AsciiCmd(const char* _cmd, size_t _len, StreamSink &_responseChannel)
{
    /*---------------------------- ↓ Add Your CMDs Here ↓ -----------------------------*/
    if (_cmd[0] == '!' || !dummy.IsEnabled())
    {
        // 最高优先级：急停 / 暂停 / 恢复（精确前缀匹配）
        if (MatchExact(_cmd, "!STOP"))
        {
            dummy.commandHandler.EmergencyStop();
            Respond(_responseChannel,
                    "ESTOP|J:%.2f,%.2f,%.2f,%.2f,%.2f,%.2f|STATE:%d|QUEUE:%lu",
                    dummy.currentJoints.a[0], dummy.currentJoints.a[1],
                    dummy.currentJoints.a[2], dummy.currentJoints.a[3],
                    dummy.currentJoints.a[4], dummy.currentJoints.a[5],
                    (int)dummy.robotState,
                    (unsigned long)dummy.commandHandler.GetSpace());
            return;
        }
        if (MatchExact(_cmd, "!PAUSE"))
        {
            dummy.commandHandler.Pause();
            Respond(_responseChannel,
                    "PAUSED|J:%.2f,%.2f,%.2f,%.2f,%.2f,%.2f|STATE:%d|QUEUE:%lu",
                    dummy.currentJoints.a[0], dummy.currentJoints.a[1],
                    dummy.currentJoints.a[2], dummy.currentJoints.a[3],
                    dummy.currentJoints.a[4], dummy.currentJoints.a[5],
                    (int)dummy.robotState,
                    (unsigned long)dummy.commandHandler.GetSpace());
            return;
        }
        if (MatchExact(_cmd, "!RESUME"))
        {
            int oldState = (int)dummy.robotState;
            dummy.commandHandler.Resume();
            if (oldState != DummyRobot::STATE_NORMAL)
                Respond(_responseChannel, "RESUMED|FROM_STATE:%d|SPEED:%.1f|QUEUE:%lu",
                        oldState, dummy.GetJointSpeed(),
                        (unsigned long)dummy.commandHandler.GetSpace());
            else
                Respond(_responseChannel, "ALREADY_NORMAL|QUEUE:%lu",
                        (unsigned long)dummy.commandHandler.GetSpace());
            return;
        }
        // !CLOSE 精确匹配（与 USB 通道同语义）
        if (MatchExact(_cmd, "!CLOSE"))
        {
            if (dummy.robotState != DummyRobot::STATE_NORMAL)
            {
                Respond(_responseChannel, "rejected: robot in state %d", (int)dummy.robotState);
            }
            else
            {
                dummy.Shutdown();
                Respond(_responseChannel, "Closed ok");
            }
            return;
        }

        if (false)
        {
        } else if (MatchExact(_cmd, "!START"))
        {
            dummy.Startup();
            Respond(_responseChannel, "Started ok");
        } else if (MatchExact(_cmd, "!HOME"))
        {
            if (!dummy.IsEnabled())
            {
                Respond(_responseChannel, "rejected: not_enabled, send !START first");
            }
            else
            {
                dummy.Homing();
                Respond(_responseChannel, "Started ok");
            }
        } else if (MatchExact(_cmd, "!CALIBRATION"))
        {
            dummy.CalibrateHomeOffset();
            Respond(_responseChannel, "calibration ok");
        } else if (MatchExact(_cmd, "!RESET"))
        {
            if (!dummy.IsEnabled())
            {
                Respond(_responseChannel, "rejected: not_enabled, send !START first");
            }
            else
            {
                dummy.Resting();
                Respond(_responseChannel, "Started ok");
            }
        }
    } else if (_cmd[0] == '#')
    {
        if (MatchExact(_cmd, "#GETJPOS"))
        {
            Respond(_responseChannel, "ok %.2f %.2f %.2f %.2f %.2f %.2f",
                    dummy.currentJoints.a[0], dummy.currentJoints.a[1],
                    dummy.currentJoints.a[2], dummy.currentJoints.a[3],
                    dummy.currentJoints.a[4], dummy.currentJoints.a[5]);
        } else if (MatchExact(_cmd, "#GETLPOS"))
        {
            dummy.UpdateJointPose6D();
            Respond(_responseChannel, "ok %.2f %.2f %.2f %.2f %.2f %.2f",
                    dummy.currentPose6D.X, dummy.currentPose6D.Y,
                    dummy.currentPose6D.Z, dummy.currentPose6D.A,
                    dummy.currentPose6D.B, dummy.currentPose6D.C);
        } else if (MatchExact(_cmd, "#GETSPEED"))
        {
            Respond(_responseChannel, "ok SPEED:%.2f", dummy.GetJointSpeed());
        } else if (MatchExact(_cmd, "#CMDMODE"))
        {
            uint32_t mode;
            sscanf(_cmd, "#CMDMODE %lu", &mode);
            dummy.SetCommandMode(mode);
            Respond(_responseChannel, "Set command mode to [%lu]", mode);
        } else
            Respond(_responseChannel, "ok");
    } else if (_cmd[0] == '>' || _cmd[0] == '@' || _cmd[0] == '&')
    {
        uint32_t freeSize = dummy.commandHandler.Push(_cmd);
        if (freeSize == 0xFC)
            Respond(_responseChannel, "rejected: not_homed, send !HOME first");
        else if (freeSize == 0xFD)
            Respond(_responseChannel, "rejected: not_enabled, send !START first");
        else if (freeSize == 0xFE)
            Respond(_responseChannel, "rejected: state=%d", (int)dummy.robotState);
        else if (freeSize == 0xFF)
            Respond(_responseChannel, "rejected: queue_full");
        else
            Respond(_responseChannel, "%d", freeSize);
    }
/*---------------------------- ↑ Add Your CMDs Here ↑ -----------------------------*/
}


void OnUart5AsciiCmd(const char* _cmd, size_t _len, StreamSink &_responseChannel)
{
    /*---------------------------- ↓ Add Your CMDs Here ↓ -----------------------------*/

/*---------------------------- ↑ Add Your CMDs Here ↑ -----------------------------*/
}
