// ============================================================================
//  interface_can.cpp —— 42 步进子板 CAN 命令处理
// ----------------------------------------------------------------------------
//  - 接收 ref 主控下发的使能、位置、速度、电流、PID/DCE 参数等命令
//  - 将本地电机状态、角度、参数和在线心跳通过 CAN 回报给主控
//  - 该协议与 35motor 结构镜像一致，仅默认电机参数不同
// ============================================================================

#include "common_inc.h"
#include "configurations.h"
#include <can.h>


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
    float tmpF;
    int32_t tmpI;

#ifdef J1_SOUND_EXPERIMENT
    // 任何运动、失能、校准或参数修改先终止声音；查询不干扰短音。
    if (_cmd < 0x20 || _cmd >= 0x7d) motor.controller->soundPulse.Cancel();
    if (_cmd == 0x30) {
        if (_len != 8 || boardConfig.canNodeId != 1 || _data[0] > 3) return;
        const uint32_t mask = __get_PRIMASK();
        __disable_irq();
        uint8_t result = 0;
        if (_data[0] == 0) motor.controller->soundPulse.Cancel();
        if (_data[0] == 1) {
            const uint16_t frequency = _data[2] | (uint16_t(_data[3]) << 8);
            const uint16_t duration = _data[4] | (uint16_t(_data[5]) << 8);
            const uint16_t amplitude = _data[6] | (uint16_t(_data[7]) << 8);
            result = encoderCalibrator.isTriggered ? 4 :
                motor.controller->StartSound(frequency, duration, amplitude);
        }
        if (_data[0] == 3) result = encoderCalibrator.isTriggered ? 4 :
            motor.controller->PrepareSoundHold();
        uint8_t reply[8] = {0x53, _data[1], result,
            static_cast<uint8_t>(motor.controller->soundPulse.state), 2,
            static_cast<uint8_t>(motor.controller->soundHoldActive),
            static_cast<uint8_t>(motor.controller->SoundHoldReady()), 0};
        __set_PRIMASK(mask);
        txHeader.StdId = (boardConfig.canNodeId << 7) | 0x30;
        CAN_Send(&txHeader, reply);
        return;
    }
    // 单节点实验锁存后仅接受查询和专用实验帧，禁止其他入口改变目标或持久参数。
    if (motor.controller->soundHoldActive && !(_cmd >= 0x20 && _cmd <= 0x2b)) return;
#endif

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
            motor.config.motionParams.ratedVelocity =
                (int32_t) (*(float*) (RxData + 4) * (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS);
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
            motor.config.motionParams.ratedVelocityAcc = (int32_t) tmpF;
            motor.motionPlanner.velocityTracker.SetVelocityAcc(motor.config.motionParams.ratedVelocityAcc);
            motor.motionPlanner.positionTracker.SetVelocityAcc(motor.config.motionParams.ratedVelocityAcc);
            boardConfig.velocityAcc = motor.config.motionParams.ratedVelocityAcc;
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

