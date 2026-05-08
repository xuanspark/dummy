// ============================================================================
//  timer.hpp —— 用户定时器 (TIM7/10/11/13/14) 的 C++ 包装
// ----------------------------------------------------------------------------
//  - 构造时绑定 TIM 句柄 + 周期（µs）
//  - SetCallback 注册中断回调，Start/Stop 启停定时器
//  - 5 个槽位（每个 TIM 一槽），由 TimerCallbackTable 静态分发
// ============================================================================
#ifndef REF_STM32F4_TIMER_HPP
#define REF_STM32F4_TIMER_HPP

#include <cmath>
#include "tim.h"

// 用户定时器中断回调函数原型。无参主体调用、仅运行在中断上下文。
typedef void (*TimerCallback_t)();

class Timer
{
private:
    TIM_HandleTypeDef *htim;        // HAL TIM 句柄。仅支持 TIM7/10/11/13/14 这五个
    uint32_t freq;                  // 设定频率（Hz）
    uint16_t PSC = 83;              // 预分频默认值（对应 1 MHz 计数频率）
    uint16_t ARR = 9999;            // 自动重装载默认值（对应 100 Hz）

    // 根据需要频率反推 PSC + ARR。使用 168 MHz 总线。
    void CalcRegister(uint32_t _freqHz);

public:
    // _htim 必须是 htim7/10/11/13/14 之一，_freqHz 默认 100 Hz。
    // 构造函数仅保存参数、不启动定时器。
    explicit Timer(TIM_HandleTypeDef *_htim, uint32_t _freqHz = 100);

    // 设定中断回调。内部会根据 htim->Instance 填入 timerCallbacks[] 静态表，
    // HAL_TIM_PeriodElapsedCallback 按 TIM 实例查表分发。
    void SetCallback(TimerCallback_t _timerCallback);

    // 启动 TIM + IT，开始周期性产生中断。
    void Start();
};

#endif //REF_STM32F4_TIMER_HPP —— 头文件包含守卫结束
