// ============================================================================
//  interface_uart.cpp —— 42 子板 UART 调试命令
// ----------------------------------------------------------------------------
//  - 由 USART RX 中断中调本函数，简易 ASCII 协议（前缀字符 + 浮点数字）：
//      "c<x>" → 设电流（A）
//      "v<x>" → 设速度（r/s）
//      "p<x>" → 设位置（圈）
//  - 主要用于现场调试 / 厂校；正式工作通信走 CAN 总线
// ============================================================================
#include "common_inc.h"
#include "configurations.h"
#include <string.h>
#include <stdlib.h>

extern Motor motor;

// UART RX 入口。_data 为 ASCII 缓冲、_len 为长度（含前缀字符）。
void OnUartCmd(uint8_t* _data, uint16_t _len)
{
    float cur, pos, vel, time;
    char *token = NULL;
    switch (_data[0])
    {
        case 'c':   // 设电流（单位 A）
            token = strtok((char*) _data, "c");
            cur = atof(token);
            if (motor.controller->modeRunning != Motor::MODE_COMMAND_CURRENT)
                motor.controller->SetCtrlMode(Motor::MODE_COMMAND_CURRENT);
            motor.controller->SetCurrentSetPoint((int32_t) (cur * 1000));
            break;
        case 'v':   // 设速度（单位 r/s）
            token = strtok((char*) _data, "v");
            vel = atof(token);
            if (motor.controller->modeRunning != Motor::MODE_COMMAND_VELOCITY)
            {
                motor.config.motionParams.ratedVelocity = boardConfig.velocityLimit;
                motor.controller->SetCtrlMode(Motor::MODE_COMMAND_VELOCITY);
            }
            motor.controller->SetVelocitySetPoint(
                (int32_t) (vel * (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS));
            break;
        case 'p':   // 设位置（单位 圈）
            token = strtok((char*) _data, "p");
            pos = atof(token);
            if (motor.controller->modeRunning != Motor::MODE_COMMAND_POSITION)
                motor.controller->requestMode = Motor::MODE_COMMAND_POSITION;

            motor.controller->SetPositionSetPoint(
                (int32_t) (pos * (float) motor.MOTOR_ONE_CIRCLE_SUBDIVIDE_STEPS));
            break;
        default:
            printf("Only support [c] [v] [p] commands!\r\n");
            break;
    }
}

