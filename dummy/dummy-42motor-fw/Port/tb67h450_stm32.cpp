// ============================================================================
//  tb67h450_stm32.cpp —— TB67H450 STM32 实现
// ----------------------------------------------------------------------------
//  - 通过 HAL_TIM_PWM_Start_IT + 直写 CCR 寄存器调整两相 PWM 占空比
//  - 配合 sin_map 完成微步输出
// ============================================================================
#include "tb67h450_stm32.h"
#include "tim.h"

void TB67H450::InitGpio()
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 信号 GPIO 端口时钟使能 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 配置信号引脚初始输出电平 */
    HAL_GPIO_WritePin(GPIOA, HW_ELEC_BM_Pin | HW_ELEC_BP_Pin | HW_ELEC_AM_Pin | HW_ELEC_AP_Pin,
                      GPIO_PIN_RESET);

    /* 配置信号引脚：PA 口多个引脚 */
    GPIO_InitStruct.Pin = HW_ELEC_BM_Pin | HW_ELEC_BP_Pin | HW_ELEC_AM_Pin | HW_ELEC_AP_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}


void TB67H450::InitPwm()
{
    MX_TIM2_Init();
}


void TB67H450::DacOutputVoltage(uint16_t _voltageA_3300mVIn12bits, uint16_t _voltageB_3300mVIn12bits)
{
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, _voltageA_3300mVIn12bits >> 2);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, _voltageB_3300mVIn12bits >> 2);
}


void TB67H450::SetInputA(bool _statusAp, bool _statusAm)
{
    _statusAp ? (GPIOA->BSRR = GPIO_PIN_5) : (GPIOA->BRR = GPIO_PIN_5);
    _statusAm ? (GPIOA->BSRR = GPIO_PIN_4) : (GPIOA->BRR = GPIO_PIN_4);
}


void TB67H450::SetInputB(bool _statusBp, bool _statusBm)
{
    _statusBp ? (GPIOA->BSRR = GPIO_PIN_3) : (GPIOA->BRR = GPIO_PIN_3);
    _statusBm ? (GPIOA->BSRR = GPIO_PIN_2) : (GPIOA->BRR = GPIO_PIN_2);
}
