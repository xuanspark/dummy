// ============================================================================
//  led_stm32.h —— LedBase 在 STM32 平台的实现
// ----------------------------------------------------------------------------
//  - 派生 LedBase：用 HAL_GPIO_WritePin 控制 LED 引脚
// ============================================================================
#ifndef CTRL_STEP_FW_LED_STM32_H
#define CTRL_STEP_FW_LED_STM32_H

#include "led_base.h"


class Led :public LedBase
{

private:
    void SetLedState(uint8_t _id, bool _state) override;

};

#endif
