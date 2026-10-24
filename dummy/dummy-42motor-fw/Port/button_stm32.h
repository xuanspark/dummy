// ============================================================================
//  button_stm32.h —— ButtonBase 在 STM32 平台的实现
// ----------------------------------------------------------------------------
//  - 派生 ButtonBase：实现 ReadButtonPinIO 通过 HAL_GPIO_ReadPin 读电平
//  - 按键 1 接 PB?，按键 2 接 PB?（具体引脚由 CubeMX 配置）
// ============================================================================
#ifndef CTRL_STEP_FW_BUTTON_STM32_H
#define CTRL_STEP_FW_BUTTON_STM32_H

#include "button_base.h"

class Button : public ButtonBase
{
public:
    explicit Button(uint8_t _id) : ButtonBase(_id)
    {}

    Button(uint8_t _id, uint32_t _longPressTime) : ButtonBase(_id, _longPressTime)
    {}

    bool IsPressed();

private:
    bool ReadButtonPinIO(uint8_t _id) override;
};

#endif
