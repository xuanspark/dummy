// ============================================================================
//  timer.cpp —— Timer 类实现：注册回调到 timerCallbacks[] 静态表，
//  在 HAL_TIM_PeriodElapsedCallback 中按 TIM 实例查表分发。
// ============================================================================
#include "timer.hpp"

// 5 个回调槽位，索引与 TIM 实例一一对应：
//   [0]=TIM7  [1]=TIM10  [2]=TIM11  [3]=TIM13  [4]=TIM14
static TimerCallback_t timerCallbacks[5];

// 构造函数：验证 TIM 实例、限付频率、计算 PSC/ARR、构释 HAL TIM 基底。
// _freqHz 限付范围 [1, 10 MHz]。验证不过调 Error_Handler 陶坏。
Timer::Timer(TIM_HandleTypeDef *_htim, uint32_t _freqHz)
{
    htim7.Instance = TIM7;
    htim10.Instance = TIM10;
    htim11.Instance = TIM11;
    htim13.Instance = TIM13;
    htim14.Instance = TIM14;

    if (!(_htim->Instance == TIM7 ||
          _htim->Instance == TIM10 ||
          _htim->Instance == TIM11 ||
          _htim->Instance == TIM13 ||
          _htim->Instance == TIM14))
    {
        Error_Handler();
    }

    if (_freqHz < 1) _freqHz = 1;
    else if (_freqHz > 10000000) _freqHz = 10000000;

    htim = _htim;
    freq = _freqHz;

    CalcRegister(freq);

    HAL_TIM_Base_DeInit(_htim);
    _htim->Init.Prescaler = PSC - 1;
    _htim->Init.CounterMode = TIM_COUNTERMODE_UP;
    _htim->Init.Period = ARR - 1;
    _htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    _htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_Base_Init(_htim) != HAL_OK)
    {
        Error_Handler();
    }
}

// Start: 启动 TIM + IT，开始周期性产生中断。
// 调用前需 SetCallback，否则中断里会调空函数指针造成坏。
void Timer::Start()
{
    HAL_TIM_Base_Start_IT(htim);
}

// CalcRegister: 根据需要频率反推 PSC 与 ARR。思路：
//   1. PSC 从 1 开始不断×2 探路，直到 ARR < 65536。
//   2. APB1 (TIM7/13/14) 计数频率 = 84 MHz；APB2 (TIM10/11) = 168 MHz。
void Timer::CalcRegister(uint32_t _freq)
{
    float psc = 0.5;
    float arr;

    do
    {
        psc *= 2;
        arr = 84000000.0f / psc / (float) _freq;
    } while (arr > 65535);

    if (htim->Instance == TIM7 || htim->Instance == TIM13 || htim->Instance == TIM14) // APB1 @84MHz
    {
        PSC = (uint16_t) round((double) psc);
        ARR = (uint16_t) (84000000.0f / (float) _freq / psc);
    } else if (htim->Instance == TIM10 || htim->Instance == TIM11) // APB2 @168MHz
    {
        PSC = (uint16_t) round((double) psc) * 2;
        ARR = (uint16_t) (84000000.0f / (float) _freq / psc);
    }
}


// SetCallback: 根据 htim->Instance 填入静态表。OnTimerCallback 会查表分发。
void Timer::SetCallback(TimerCallback_t _timerCallback)
{
    if (htim->Instance == TIM7)
    {
        timerCallbacks[0] = _timerCallback;
    } else if (htim->Instance == TIM10)
    {
        timerCallbacks[1] = _timerCallback;
    } else if (htim->Instance == TIM11)
    {
        timerCallbacks[2] = _timerCallback;
    } else if (htim->Instance == TIM13)
    {
        timerCallbacks[3] = _timerCallback;
    } else if (htim->Instance == TIM14)
    {
        timerCallbacks[4] = _timerCallback;
    }
}


// HAL_TIM_PeriodElapsedCallback 中会调本函数。根据 TIM 实例查静态表分发到
// 用户设定的回调函数。运行在中断上下文，调用方需避免在里面调耗时 API。
extern "C"
void OnTimerCallback(TIM_TypeDef *timInstance)
{
    if (timInstance == TIM7)
    {
        timerCallbacks[0]();
    } else if (timInstance == TIM10)
    {
        timerCallbacks[1]();
    } else if (timInstance == TIM11)
    {
        timerCallbacks[2]();
    } else if (timInstance == TIM13)
    {
        timerCallbacks[3]();
    } else if (timInstance == TIM14)
    {
        timerCallbacks[4]();
    }
}