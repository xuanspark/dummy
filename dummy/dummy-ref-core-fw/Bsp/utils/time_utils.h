// ============================================================================
//  time_utils.h —— 高精度时间计数（µs / ms）工具
// ----------------------------------------------------------------------------
//  - micros()  返回自启动以来的微秒数（基于 SysTick 当前计数 + HAL tick）
//  - millis()  毫秒计数（HAL_GetTick 包装）
//  - delayUs() 阻塞延时
// ============================================================================
#ifndef REF_STM32F4_TIME_UTILS_H
#define REF_STM32F4_TIME_UTILS_H

// 设为 C 链接以供 CubeMX C 代码与 C++ 部分同时调用
#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

// 返回从启动到当前的微秒数。内部用 HAL_GetTick × 1000 + SysTick 剩余计数拼出，
// 在调用间隔大于 1 毫秒时能保证单调递增。溢出周期为 71 分钟左右。
uint32_t micros(void);

// HAL_GetTick 的薄坏。返回从启动以来的毫秒计数。
uint32_t millis(void);

// 阻塞微秒级延迟。与 HAL_Delay 不同，本函数名是 Arduino 调用传统，仅供上层使用。
void delayMicroseconds(uint32_t us);

#ifdef __cplusplus
}
#endif
#endif //REF_STM32F4_TIME_UTILS_H —— 头文件包含守卫结束
