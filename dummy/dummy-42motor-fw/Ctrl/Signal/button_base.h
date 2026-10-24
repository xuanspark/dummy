// ============================================================================
//  button_base.h —— 按键状态机抽象基类
// ----------------------------------------------------------------------------
//  - 在 Tick(deltaMs) 中扫描原始电平并产生 UP / DOWN / CLICK / LONG_PRESS 事件
//  - 长按阈值由构造函数传入（main.cpp 用 1 秒 / 3 秒）
//  - 派生类 Button (button_stm32) 实现 ReadPin GPIO 抽象
// ============================================================================
#ifndef CTRL_STEP_FW_BUTTON_BASE_H
#define CTRL_STEP_FW_BUTTON_BASE_H

#include <cstdint>

class ButtonBase
{
public:
    enum Event
    {
        UP,
        DOWN,
        LONG_PRESS,
        CLICK
    };

    explicit ButtonBase(uint8_t _id) :
        id(_id)
    {}

    ButtonBase(uint8_t _id, uint32_t _longPressTime) :
        id(_id), longPressTime(_longPressTime)
    {}

    void Tick(uint32_t _timeElapseMillis);
    void SetOnEventListener(void (* _callback)(Event));

protected:
    uint8_t id;
    bool lastPinIO{};
    uint32_t timer=0;
    uint32_t pressTime{};
    uint32_t longPressTime = 2000;

    void (* OnEventFunc)(Event){};

    virtual bool ReadButtonPinIO(uint8_t _id) = 0;
};

#endif
