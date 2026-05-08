// ============================================================================
//  common_inc.h —— 用户应用公共头文件汇总（35 步进子板）
// ----------------------------------------------------------------------------
//  把 main.cpp / 协议层 / 驱动层等所需的标准头一次性 #include
// ============================================================================
#ifndef LOOP_H
#define LOOP_H

#ifdef __cplusplus
extern "C" {
#endif
/*---------------------------- C 作用域 ---------------------------*/
// 以下函数声明在 C 与 C++ 中都可见，供 CubeMX 生成的代码与 freertos 任务调用
#include "stdint-gcc.h"

void Main();                                            // 主入口（由 CubeMX 主循环调用）
void OnUartCmd(uint8_t* _data, uint16_t _len);          // UART 协议层入口
void OnCanCmd(uint8_t _cmd, uint8_t* _data, uint32_t _len);  // CAN 协议层入口（按 cmd 分发）

#ifdef __cplusplus
}
/*---------------------------- C++ 作用域 ---------------------------*/
// 仅 C++ 部分可见，包含驱动 / 编码器 / 按键 / LED 的 STM32 端口适配
#include <cstdio>
#include "Motor/motor.h"
#include "mt6816_stm32.h"
#include "tb67h450_stm32.h"
#include "encoder_calibrator_stm32.h"
#include "button_stm32.h"
#include "led_stm32.h"
#include "adc.h"
#endif
#endif
