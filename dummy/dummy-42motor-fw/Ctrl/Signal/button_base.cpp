// ============================================================================
//  button_base.cpp —— 按键状态机实现
// ----------------------------------------------------------------------------
//  - Tick(deltaMs) 内根据原始电平 + 计时器产生事件并调 OnEventListener
//  - 短按 = 抬起前未触发长按；长按 = 持续低电平超过阈值
// ============================================================================
#include "button_base.h"

void ButtonBase::Tick(uint32_t _timeElapseMillis)
{
    timer += _timeElapseMillis;
    bool pinIO = ReadButtonPinIO(id);

    if (lastPinIO != pinIO)
    {
        if (pinIO)
        {
            OnEventFunc(UP);
            if (timer - pressTime > longPressTime)
                OnEventFunc(LONG_PRESS);
            else
                OnEventFunc(CLICK);
        } else
        {
            OnEventFunc(DOWN);
            pressTime = timer;
        }

        lastPinIO = pinIO;
    }
}

void ButtonBase::SetOnEventListener(void (* _callback)(Event))
{
    lastPinIO =  ReadButtonPinIO(id);

    OnEventFunc = _callback;
}
