// ============================================================================
//  interface_can.cpp —— 35 子板 CAN 命令分发器
// ----------------------------------------------------------------------------
//  StdId 带本机节点 ID 后与表下命令号包含。在 can.c 的 RX 中断里会调本函数：
//
//    0x01-0x0F 控制类（不写 EEPROM）：使能 / 校准 / 电流 / 速度 / 位置设点
//    0x10-0x1F 配置类（可选写 EEPROM）：NodeID / 限制 / DCE / 堆转保护
//    0x20-0x2A 查询类：读电流 / 速度 / 位置 / 偏移 / 温度 / DCE 参数
//    0x7D-0x7F 维护类：温度开关 / 擦除配置 / 重启
//
//  与 42motor 版完全镜像，所有命令号一致。
//  _data[0..3] 是负载（0~3 字节，常为 float / int32），_data[4] 是 "是否需要 ACK / 写 EEPROM" 标志。
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

    // 注意：_data 与 RxData 是同一缓冲（见 can.c：OnCanCmd(cmd, RxData, ...)）。
    // **千万不能在 switch 前对 _data[4..7] 做"防御性清零"**，否则会破坏需要读取
    // RxData[4..7] 的命令负载（最致命的是 0x07 的速度 float 占 [4..7]，
    // 一旦被清零 → ratedVelocity≈0 → softVelocity 始终 0 → 电机不动、状态恒
    // STATE_RUNNING → LED0 持续心跳，但物理无任何动作）。
    // 应答帧里若担心 stale 字节污染上报，应在每个写应答的分支里**单独清 _data[7]**。

    switch (_cmd)
    {
        // ===== 0x01-0x0F 控制类命令（不写 EEPROM）=====
        case 0x01:  // 使能 / 失能电机（使能进入默认速度模式）
            motor.controller->requestMode = (*(uint32_t*) (RxData) == 1) ?
                                            Motor::MODE_COMMAND_VELOCITY : Motor::MODE_STOP;
            break;
        case 0x02:  // 触发编码器校准
            encoderCalibrator.isTriggered = true;
            break;
        case 0x03:  // 设电流设点（float, A）
            if (motor.controller->modeRunning != Motor::MODE_COMMAND_CURRENT)
                motor.controller->SetCtrlMode(Motor::MODE_COMMAND_CURRENT);
            motor.controller->SetCurrentSetPoint((int32_t) (*(float*) RxData * 1000));
            break;
        case 0x04:  // 设速度设点（float, r/s）
            if (motor.controller->modeRunning != Motor::MODE_COMMAND_VELOCITY)
            {
                motor.config.motionParams.ratedVelocity = boardConfig.velocityLimit;
                motor.controller->SetCtrlMode(Motor::MODE_COMMAND_VELOCITY);
            }
            motor.controller->SetVelocitySetPoint(
                (int32_t) (*(float*) RxData *
                           (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS));
            break;
        case 0x05:  // 设位置设点（float, 圈）
            // 手爪专用：每次位置命令都清一次堆转标志。
            // 1) 从电流模式切过来时避免 sticky stall 使电机休眠；
            // 2) 位置模式本身复试不同目标时，上次达到机械限位产生的 stall 也能被清。
            motor.controller->ClearStallFlag();
            if (motor.controller->modeRunning != Motor::MODE_COMMAND_POSITION)
            {
                motor.config.motionParams.ratedVelocity = boardConfig.velocityLimit;
                motor.controller->SetCtrlMode(Motor::MODE_COMMAND_POSITION);
            }
            motor.controller->SetPositionSetPoint(
                (int32_t) (*(float*) RxData * (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS));
            // 注意：曾在此处有 printf 调试输出，已删除——OnCanCmd 在 CAN RX 中断里调用，
            // printf 走 UART 阻塞输出 ~100µs+，会导致后续 CAN 帧丢失或抖动。
            if (_data[4]) // Need Position & Finished ACK
            {
                tmpF = motor.controller->GetPosition();
                auto* b = (unsigned char*) &tmpF;
                for (int i = 0; i < 4; i++)
                    _data[i] = *(b + i);
                _data[4] = motor.controller->state == Motor::STATE_FINISH ? 1 : 0;
                // 手爪固件扩展：未用字节携带堆转与完整状态枚举，老 ref 不读不受影响
                _data[5] = motor.controller->isStalled ? 1 : 0;
                _data[6] = (uint8_t) motor.controller->state;
                _data[7] = 0;
                txHeader.StdId = (boardConfig.canNodeId << 7) | 0x23;
                CAN_Send(&txHeader, _data);
            }
            break;
        case 0x06:  // 设“位置 + 到达时间”（float pos、float time）
            // 同 0x05：每次调用都清 stall，位置模式也可复试
            motor.controller->ClearStallFlag();
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
                _data[5] = motor.controller->isStalled ? 1 : 0;
                _data[6] = (uint8_t) motor.controller->state;
                _data[7] = 0;
                txHeader.StdId = (boardConfig.canNodeId << 7) | 0x23;
                CAN_Send(&txHeader, _data);
            }
            break;
        case 0x07:  // 设“位置 + 速度限制”（float pos、float vel）
        {
            // 同 0x05：每次调用都清 stall，位置模式也可复试
            motor.controller->ClearStallFlag();
            if (motor.controller->modeRunning != Motor::MODE_COMMAND_POSITION)
            {
                motor.config.motionParams.ratedVelocity = boardConfig.velocityLimit;
                motor.controller->SetCtrlMode(Motor::MODE_COMMAND_POSITION);
            }
            motor.config.motionParams.ratedVelocity =
                (int32_t) (*(float*) (RxData + 4) * (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS);
            motor.controller->SetPositionSetPoint(
                (int32_t) (*(float*) RxData * (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS));
            // 二参数命令总是上报 ACK
            tmpF = motor.controller->GetPosition();
            auto* b = (unsigned char*) &tmpF;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            _data[4] = motor.controller->state == Motor::STATE_FINISH ? 1 : 0;
            _data[5] = motor.controller->isStalled ? 1 : 0;
            _data[6] = (uint8_t) motor.controller->state;
            _data[7] = 0;
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x23;
            CAN_Send(&txHeader, _data);
        }
            break;

        // ===== 0x10-0x1F 配置类命令（_data[4]=1 时写入 EEPROM） =====
        case 0x11:  // 设节点 ID（可写 EEPROM）
            boardConfig.canNodeId = *(uint32_t*) (RxData);
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x12:  // 设额定电流（可写 EEPROM）
            motor.config.motionParams.ratedCurrent = (int32_t) (*(float*) RxData * 1000);
            boardConfig.currentLimit = motor.config.motionParams.ratedCurrent;
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x13:  // 设额定速度（可写 EEPROM）
            motor.config.motionParams.ratedVelocity =
                (int32_t) (*(float*) RxData *
                           (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS);
            boardConfig.velocityLimit = motor.config.motionParams.ratedVelocity;
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x14:  // 设额定加速度（可写 EEPROM）
            tmpF = *(float*) RxData * (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS;

            motor.config.motionParams.ratedVelocityAcc = (int32_t) tmpF;
            motor.motionPlanner.velocityTracker.SetVelocityAcc((int32_t) tmpF);
            motor.motionPlanner.positionTracker.SetVelocityAcc((int32_t) tmpF);
            boardConfig.velocityAcc = motor.config.motionParams.ratedVelocityAcc;
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x15:  // 将当前位置设为零点（强制写 EEPROM）
            motor.controller->ApplyPosAsHomeOffset();
            // 手爪行程跨多圈，必须保存完整 homeOffset，不能再做 % SUBDIVIDE。
            boardConfig.encoderHomeOffset = motor.config.motionParams.encoderHomeOffset;
            boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x16:  // 设上电自动使能（可写 EEPROM）
            boardConfig.enableMotorOnBoot = (*(uint32_t*) (RxData) == 1);
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x17:  // 设 DCE Kp（位置环比例，可写 EEPROM）
            motor.config.ctrlParams.dce.kp = *(int32_t*) (RxData);
            boardConfig.dce_kp = motor.config.ctrlParams.dce.kp;
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x18:  // 设 DCE Kv（速度环比例，可写 EEPROM）
            motor.config.ctrlParams.dce.kv = *(int32_t*) (RxData);
            boardConfig.dce_kv = motor.config.ctrlParams.dce.kv;
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x19:  // 设 DCE Ki（积分，可写 EEPROM）
            motor.config.ctrlParams.dce.ki = *(int32_t*) (RxData);
            boardConfig.dce_ki = motor.config.ctrlParams.dce.ki;
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x1A:  // 设 DCE Kd（微分，可写 EEPROM）
            motor.config.ctrlParams.dce.kd = *(int32_t*) (RxData);
            boardConfig.dce_kd = motor.config.ctrlParams.dce.kd;
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;
        case 0x1B:  // 设堆转保护开关（可写 EEPROM）
            motor.config.ctrlParams.stallProtectSwitch = (*(uint32_t*) (RxData) == 1);
            boardConfig.enableStallProtect = motor.config.ctrlParams.stallProtectSwitch;
            if (_data[4])
                boardConfig.configStatus = CONFIG_COMMIT;
            break;


        // ===== 0x20-0x2A 查询类命令 =====
        case 0x21: // 读电流（FOC 环路采样值）
        {
            tmpF = motor.controller->GetFocCurrent();
            auto* b = (unsigned char*) &tmpF;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            _data[4] = (motor.controller->state == Motor::STATE_FINISH ? 1 : 0);
            _data[5] = motor.controller->isStalled ? 1 : 0;
            _data[6] = (uint8_t) motor.controller->state;
            _data[7] = 0;

            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x21;
            CAN_Send(&txHeader, _data);
        }
            break;
        case 0x22: // 读速度
        {
            tmpF = motor.controller->GetVelocity();
            auto* b = (unsigned char*) &tmpF;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            _data[4] = (motor.controller->state == Motor::STATE_FINISH ? 1 : 0);
            _data[5] = motor.controller->isStalled ? 1 : 0;
            _data[6] = (uint8_t) motor.controller->state;
            _data[7] = 0;

            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x22;
            CAN_Send(&txHeader, _data);
        }
            break;
        case 0x23: // 读位置（带完成状态位）
        {
            tmpF = motor.controller->GetPosition();
            auto* b = (unsigned char*) &tmpF;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            // 完成 ACK（1 = STATE_FINISH）
            _data[4] = motor.controller->state == Motor::STATE_FINISH ? 1 : 0;
            // 手爪固件扩展：_data[5] = isStalled (0/1)，_data[6] = state 枚举
            _data[5] = motor.controller->isStalled ? 1 : 0;
            _data[6] = (uint8_t) motor.controller->state;
            _data[7] = 0;
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x23;
            CAN_Send(&txHeader, _data);
//            printf("CAN SEND BACK to NODE[%d]\n", boardConfig.canNodeId );
        }
            break;
        case 0x24: // 读零点偏移
        {
            tmpI = motor.config.motionParams.encoderHomeOffset;
            auto* b = (unsigned char*) &tmpI;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            _data[4] = 0; _data[5] = 0; _data[6] = 0; _data[7] = 0;
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x24;
            CAN_Send(&txHeader, _data);
        }
            break;

        case 0x25: // 读电机温度
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

        case 0x27: // 读 DCE Kp
        {
            auto* b = (unsigned char*) &boardConfig.dce_kp;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            _data[4] = 0; _data[5] = 0; _data[6] = 0; _data[7] = 0;
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x27;
            CAN_Send(&txHeader, _data);
        }
            break;
        case 0x28: // 读 DCE Kv
        {
            auto* b = (unsigned char*) &boardConfig.dce_kv;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            _data[4] = 0; _data[5] = 0; _data[6] = 0; _data[7] = 0;
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x28;
            CAN_Send(&txHeader, _data);
        }
            break;
        case 0x29: // 读 DCE Ki
        {
            auto* b = (unsigned char*) &boardConfig.dce_ki;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            _data[4] = 0; _data[5] = 0; _data[6] = 0; _data[7] = 0;
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x29;
            CAN_Send(&txHeader, _data);
        }
            break;
        case 0x2A: // 读 DCE Kd
        {
            auto* b = (unsigned char*) &boardConfig.dce_kd;
            for (int i = 0; i < 4; i++)
                _data[i] = *(b + i);
            _data[4] = 0; _data[5] = 0; _data[6] = 0; _data[7] = 0;
            txHeader.StdId = (boardConfig.canNodeId << 7) | 0x2A;
            CAN_Send(&txHeader, _data);
        }
            break;

        // ===== 0x7D-0x7F 维护类 =====
        case 0x7d:  // 启用电机温度采样
            boardConfig.enableTempWatch = true;
            break;
        case 0x7e:  // 擦除配置（写入后主循环会重启）
            boardConfig.configStatus = CONFIG_RESTORE;
            break;
        case 0x7f:  // 重启
            HAL_NVIC_SystemReset();
            break;
        default:
            break;
    }

}

