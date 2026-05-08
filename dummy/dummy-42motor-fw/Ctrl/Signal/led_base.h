// ============================================================================
//  led_base.h —— LED 状态指示器抽象基类
// ----------------------------------------------------------------------------
//  - Tick(deltaMs, motorState) 根据 motorState 切换不同闪烁模式
//  - Status(idx, on) 直接控制单个 LED
//  - 派生类 Led (led_stm32) 实现 GPIO 输出
// ============================================================================
#ifndef CTRL_STEP_FW_LED_BASE_H
#define CTRL_STEP_FW_LED_BASE_H

#include <cstdint>
#include "Motor/motor.h"

class LedBase
{
public:
    LedBase()
    = default;

    void Tick(uint32_t _timeElapseMillis, Motor::State_t _state);
    void Status(uint8_t id, bool on);

private:
    uint32_t timer = 0;
    uint32_t timerHeartBeat = 0;
    bool motorEnable = false;
    bool heartBeatEnable = false;
    uint8_t heartBeatPhase = 1;
    uint8_t blinkNum = 0;
    uint8_t targetBlinkNum = 0;
    uint32_t timerBlink = 0;
    uint8_t blinkPhase = 1;

    virtual void SetLedState(uint8_t _id, bool _state) = 0;
};


#endif
