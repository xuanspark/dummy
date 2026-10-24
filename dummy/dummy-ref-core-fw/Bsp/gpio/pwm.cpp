// ============================================================================
//  pwm.cpp —— PWM 类实现：构造时设频率，Start 启动，SetDuty 调占空比
// ============================================================================
#include "pwm.hpp"

// 构造函数：同时初始化 TIM9（A 组）与 TIM12（B 组）。
// 频率限付范围：10 Hz ≤ freq ≤ 84 kHz，超出会被夹到边界。
// Period=999 + Prescaler 控制频率，占空比分辨率定为 0.1%。
PWM::PWM(uint32_t _freqHzA, uint32_t _freqHzB)
{
    if (_freqHzA > 84000)_freqHzA = 84000;
    else if (_freqHzA < 10)_freqHzA = 10;

    if (_freqHzB > 84000)_freqHzB = 84000;
    else if (_freqHzB < 10)_freqHzB = 10;


    TIM_OC_InitTypeDef sConfigOC = {0};

    // ----- TIM9 (A 组)：168 MHz 总线，预分频 = 168000/freqA - 1、ARR=999 -----
    htim9.Instance = TIM9;
    htim9.Init.Prescaler = 168000 / _freqHzA - 1;
    htim9.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim9.Init.Period = 999;
    htim9.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim9.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim9) != HAL_OK)
    {
        Error_Handler();
    }
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim9, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_TIM_PWM_ConfigChannel(&htim9, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
    {
        Error_Handler();
    }
    HAL_TIM_MspPostInit(&htim9);

    // ----- TIM12 (B 组)：84 MHz 总线，预分频 = 84000/freqB - 1、ARR=999 -----
    htim12.Instance = TIM12;
    htim12.Init.Prescaler = 84000 / _freqHzB - 1;
    htim12.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim12.Init.Period = 999;
    htim12.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim12.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim12) != HAL_OK)
    {
        Error_Handler();
    }
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim12, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_TIM_PWM_ConfigChannel(&htim12, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
    {
        Error_Handler();
    }
    HAL_TIM_MspPostInit(&htim12);

}

// Start: 将 CCR 清零后启动指定通道的 PWM 输出。
// CH_ALL 会并行启动 4 路。
void PWM::Start(PWM::PwmChannel_t _channel)
{
    switch (_channel)
    {
        case CH_A1:
            TIM9->CCR1 = 0;
            HAL_TIM_PWM_Start(&htim9, TIM_CHANNEL_1);
            break;
        case CH_A2:
            TIM9->CCR2 = 0;
            HAL_TIM_PWM_Start(&htim9, TIM_CHANNEL_2);
            break;
        case CH_B1:
            TIM12->CCR1 = 0;
            HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_1);
            break;
        case CH_B2:
            TIM12->CCR2 = 0;
            HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_2);
            break;
        case CH_ALL:
            TIM9->CCR1 = 0;
            HAL_TIM_PWM_Start(&htim9, TIM_CHANNEL_1);
            TIM9->CCR2 = 0;
            HAL_TIM_PWM_Start(&htim9, TIM_CHANNEL_2);
            TIM12->CCR1 = 0;
            HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_1);
            TIM12->CCR2 = 0;
            HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_2);
            break;
    }
}

// SetDuty: 设置占空比。_duty 限付 [0,1]，CCR = 1000 × _duty。
// CH_ALL 同时设 4 路。不调 Start 仅设以不会输出。
void PWM::SetDuty(PWM::PwmChannel_t _channel, float _duty)
{
    if (_duty > 1)_duty = 1;
    else if (_duty < 0)_duty = 0;

    auto ccr = static_cast<uint32_t>(1000 * _duty);

    switch (_channel)
    {
        case CH_A1:
            TIM9->CCR1 = ccr;
            break;
        case CH_A2:
            TIM9->CCR2 = ccr;
            break;
        case CH_B1:
            TIM12->CCR1 = ccr;
            break;
        case CH_B2:
            TIM12->CCR2 = ccr;
            break;
        case CH_ALL:
            TIM9->CCR1 = ccr;
            TIM9->CCR2 = ccr;
            TIM12->CCR1 = ccr;
            TIM12->CCR2 = ccr;
            break;
    }
}
