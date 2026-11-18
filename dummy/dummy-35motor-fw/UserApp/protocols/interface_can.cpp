// ============================================================================
//  interface_can.cpp —— 35 步进子板 CAN 命令处理
// ----------------------------------------------------------------------------
//  - 接收 ref 主控下发的使能、位置、速度、电流、PID/DCE 参数等命令
//  - 将本地电机状态、角度、参数和在线心跳通过 CAN 回报给主控
//  - 该协议与 42motor 结构镜像一致，仅默认电机参数不同
// ============================================================================

#include "common_inc.h"
#include "configurations.h"
#include <can.h>
#include <cmath>
#include <cstring>


extern Motor motor;
extern EncoderCalibrator encoderCalibrator;

CAN_TxHeaderTypeDef txHeader =
    {
        .StdId = 0x00,
        .ExtId = 0x00,
        .IDE = CAN_ID_STD,
        .RTR = CAN_RTR_DATA,
        .DLC = 8,
        .TransmitGlobalTime = DISABLE
    };


void OnCanCmd(uint8_t _cmd, uint8_t* _data, uint32_t _len)
{
    if (_len != 8) return;
    // 位模式检查不受 Release 快速数学优化影响。
    auto finite = [](float value) {
        uint32_t bits; std::memcpy(&bits, &value, 4);
        return (bits & 0x7f800000u) != 0x7f800000u;
    };
    float input = 0, second = 0;
    std::memcpy(&input, _data, 4);
    std::memcpy(&second, _data + 4, 4);
    if (_cmd >= 0x03 && _cmd <= 0x07) {
        if (!finite(input)) return;
        const float bound = _cmd == 0x03 ? 3.3f : (_cmd == 0x04 ? 30.0f : 40000.0f);
        if (input < -bound || input > bound) return;
        if ((_cmd == 0x06 || _cmd == 0x07) && (!finite(second) || second <= 0 || second > 40000)) return;
    }
    if (_cmd >= 0x12 && _cmd <= 0x14) {
        const float maximum = _cmd == 0x12 ? 3.3f : (_cmd == 0x13 ? 30.0f : 1000.0f);
        const float minimum = _cmd == 0x14 ? 1.0f / 51200.0f : 0.0f;
        if (!finite(input) || input < minimum || input > maximum) return;
    }
    float tmpF;
    int32_t tmpI;

    switch (_cmd)
    {
        // 0x00~0x0F No Memory CMDs
        case 0x01:  // Enable Motor
            motor.controller->requestMode = (*(uint32_t*) (RxData) == 1) ?
                                            Motor::MODE_COMMAND_VELOCITY : Motor::MODE_STOP;
            break;
        case 0x02:  // Do Calibration
            encoderCalibrator.isTriggered = true;
            break;
        case 0x03:  // Set Current SetPoint
            if (motor.controller->modeRunning != Motor::MODE_COMMAND_CURRENT)
                motor.controller->SetCtrlMode(Motor::MODE_COMMAND_CURRENT);
            motor.controller->SetCurrentSetPoint((int32_t) (*(float*) RxData * 1000));
            break;
        case 0x04:  // Set Velocity SetPoint
            if (motor.controller->modeRunning != Motor::MODE_COMMAND_VELOCITY)
            {
                motor.config.motionParams.ratedVelocity = boardConfig.velocityLimit;
                motor.controller->SetCtrlMode(Motor::MODE_COMMAND_VELOCITY);
            }
            motor.controller->SetVelocitySetPoint(
                (int32_t) (*(float*) RxData *
                           (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS));
            break;
        case 0x05:  // Set Position SetPoint
            if (motor.controller->modeRunning != Motor::MODE_COMMAND_POSITION)
            {
                motor.config.motionParams.ratedVelocity = boardConfig.velocityLimit;
                motor.controller->SetCtrlMode(Motor::MODE_COMMAND_POSITION);
            }
            motor.controller->SetPositionSetPoint(
                (int32_t) (*(float*) RxData * (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS));
            if (_data[4]) // Need Position & Finished ACK
            {
                tmpF = motor.controller->GetPosition();
                auto* b = (unsigned char*) &tmpF;
                for (int i = 0; i < 4; i++)
                    _data[i] = *(b + i);
                _data[4] = motor.controller->state == Motor::STATE_FINISH ? 1 : 0;
                txHeader.StdId = (boardConfig.canNodeId << 7) | 0x23;
                CAN_Send(&txHeader, _data);
            }
            break;
        case 0x06:  // Set Position with Time
            if (motor.controller->modeRunning != Motor::MODE_COMMAND_POSITION)
                motor.controller->SetCtrlMode(Motor::MODE_COMMAND_POSITION);
            motor.controller->SetPositionSetPointWithTime(
                (int32_t) (*(float*) RxData * (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS),
                *(float*) (RxData + 4));
            if (_data[4]) // Need Position & Finished ACK
            {
                tmpF = motor.controller->GetPosition();
                auto* b = (unsigned char*) &tmpF;
                for (int i = 0; i < 4; i++)
                    _data[i] = *(b + i);
                _data[4] = motor.controller->state == Motor::STATE_FINISH ? 1 : 0;
                txHeader.StdId = (boardConfig.canNodeId << 7) | 0x23;
                CAN_Send(&txHeader, _data);
            }
            break;
        case 0x07:  // Set Position with Velocity-Limit
        {
            if (motor.controller->modeRunning != Motor::MODE_COMMAND_POSITION)
            {
                motor.config.motionParams.ratedVelocity = boardConfig.velocityLimit;
                motor.controller->SetCtrlMode(Motor::MODE_COMMAND_POSITION);
            }
            motor.config.motionParams.ratedVelocity = (int32_t) fminf(
                second * motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS, (float) boardConfig.velocityLimit);
            motor.controller->SetPositionSetPoint(
                (int32_t) (*(float*) RxData * (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS));
            // Always Need Position & Finished ACK
            tmpF = motor.controller->GetPosition();
            auto* b = (unsigned char*) &tmpF;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            _data[4] = motor.controller->state == Motor::STATE_FINISH ? 1 : 0;
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x23;
            CAN_Send(&txHeader, _data);
        }
            break;

            // 0x10~0x1F CMDs with Memory
        case 0x11:  // Set Node-ID and Store to EEPROM
            boardConfig.canNodeId = *(uint32_t*) (RxData);
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x12:  // Set Current-Limit and Store to EEPROM
            motor.config.motionParams.ratedCurrent = (int32_t) (*(float*) RxData * 1000);
            boardConfig.currentLimit = motor.config.motionParams.ratedCurrent;
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x13:  // Set Velocity-Limit and Store to EEPROM
            motor.config.motionParams.ratedVelocity =
                (int32_t) (*(float*) RxData *
                           (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS);
            boardConfig.velocityLimit = motor.config.motionParams.ratedVelocity;
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x14:  // Set Acceleration （and Store to EEPROM）
            tmpF = *(float*) RxData;
            if (!(tmpF > 0.0f))
                break;
            tmpF *= (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS;
            if (tmpF < 1.0f)
                break;
            if (_data[4]) boardConfig.velocityAcc = (int32_t) tmpF;
            tmpF = fminf(tmpF, (float) boardConfig.velocityAcc);
            motor.config.motionParams.ratedVelocityAcc = (int32_t) tmpF;
            motor.motionPlanner.velocityTracker.SetVelocityAcc(motor.config.motionParams.ratedVelocityAcc);
            motor.motionPlanner.positionTracker.SetVelocityAcc(motor.config.motionParams.ratedVelocityAcc);
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x15:  // Apply Home-Position and Store to EEPROM
            motor.controller->ApplyPosAsHomeOffset();
            boardConfig.encoderHomeOffset = motor.config.motionParams.encoderHomeOffset %
                                            motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS;
            boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x16:  // Set Auto-Enable and Store to EEPROM
            boardConfig.enableMotorOnBoot = (*(uint32_t*) (RxData) == 1);
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x17:  // Set DCE Kp
            motor.config.ctrlParams.dce.kp = *(int32_t*) (RxData);
            boardConfig.dce_kp = motor.config.ctrlParams.dce.kp;
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x18:  // Set DCE Kv
            motor.config.ctrlParams.dce.kv = *(int32_t*) (RxData);
            boardConfig.dce_kv = motor.config.ctrlParams.dce.kv;
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x19:  // Set DCE Ki
            motor.config.ctrlParams.dce.ki = *(int32_t*) (RxData);
            boardConfig.dce_ki = motor.config.ctrlParams.dce.ki;
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x1A:  // Set DCE Kd
            motor.config.ctrlParams.dce.kd = *(int32_t*) (RxData);
            boardConfig.dce_kd = motor.config.ctrlParams.dce.kd;
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x1B:  // Set Enable Stall-Protect
            motor.config.ctrlParams.stallProtectSwitch = (*(uint32_t*) (RxData) == 1);
            boardConfig.enableStallProtect = motor.config.ctrlParams.stallProtectSwitch;
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;


            // 0x20~0x2F Inquiry CMDs
        case 0x21: // Get Current
        {
            tmpF = motor.controller->GetFocCurrent();
            auto* b = (unsigned char*) &tmpF;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            _data[4] = (motor.controller->state == Motor::STATE_FINISH ? 1 : 0);

            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x21;
            CAN_Send(&txHeader, _data);
        }
            break;
        case 0x22: // Get Velocity
        {
            tmpF = motor.controller->GetVelocity();
            auto* b = (unsigned char*) &tmpF;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            _data[4] = (motor.controller->state == Motor::STATE_FINISH ? 1 : 0);

            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x22;
            CAN_Send(&txHeader, _data);
        }
            break;
        case 0x23: // Get Position
        {
            tmpF = motor.controller->GetPosition();
            auto* b = (unsigned char*) &tmpF;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            // Finished ACK
            _data[4] = motor.controller->state == Motor::STATE_FINISH ? 1 : 0;
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x23;
            CAN_Send(&txHeader, _data);
        }
            break;
        case 0x24: // Get Offset
        {
            tmpI = motor.config.motionParams.encoderHomeOffset;
            auto* b = (unsigned char*) &tmpI;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x24;
            CAN_Send(&txHeader, _data);
        }
            break;

        case 0x25: // Get temperature
        {
            auto* b = (unsigned char*) &boardConfig.motor_temperature;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            _data[4] = 0;
            _data[5] = 0;
            _data[6] = 0;
            _data[7] = 0;
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x25;
            CAN_Send(&txHeader, _data);
        }
            break;

        case 0x2c:
        case 0x2d:
        case 0x2e:
        {
            // 前四字节为保存配置，后四字节为当前运行值。
            const float scale = _cmd == 0x2e ? 1000.0f : (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS;
            float configured = (_cmd == 0x2c ? boardConfig.velocityAcc :
                (_cmd == 0x2d ? boardConfig.velocityLimit : boardConfig.currentLimit)) / scale;
            float applied = (_cmd == 0x2c ? motor.config.motionParams.ratedVelocityAcc :
                (_cmd == 0x2d ? motor.config.motionParams.ratedVelocity : motor.config.motionParams.ratedCurrent)) / scale;
            std::memcpy(_data, &configured, 4);
            std::memcpy(_data + 4, &applied, 4);
            txHeader.StdId = (boardConfig.canNodeId << 7) | _cmd;
            CAN_Send(&txHeader, _data);
            break;
        }
        case 0x2b:
            // 仅在主循环完成 EEPROM 写入后确认落盘。
            _data[0] = boardConfig.configStatus == CONFIG_OK ? 1 : 0;
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x2b;
            CAN_Send(&txHeader, _data);
            break;
        case 0x27: // Get DCE Kp
        {
            auto* b = (unsigned char*) &boardConfig.dce_kp;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x27;
            CAN_Send(&txHeader, _data);
        }
            break;
        case 0x28: // Get DCE Kv
        {
            auto* b = (unsigned char*) &boardConfig.dce_kv;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x28;
            CAN_Send(&txHeader, _data);
        }
            break;
        case 0x29: // Get DCE Ki
        {
            auto* b = (unsigned char*) &boardConfig.dce_ki;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x29;
            CAN_Send(&txHeader, _data);
        }
            break;
        case 0x2A: // Get DCE Kd
        {
            auto* b = (unsigned char*) &boardConfig.dce_kd;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x2A;
            CAN_Send(&txHeader, _data);
        }
            break;

        case 0x7d:  // enable motor temperature watch
            boardConfig.enableTempWatch = true;
            break;
        case 0x7e:  // Erase Configs
            boardConfig.configStatus = CONFIG_RESTORE;
            break;
        case 0x7f:  // Reboot
            HAL_NVIC_SystemReset();
            break;
        default:
            break;
    }

}

