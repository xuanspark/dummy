// ============================================================================
//  tb67h450_base.h —— TB67H450 双 H 桥驱动芯片抽象基类
// ----------------------------------------------------------------------------
//  - 派生 DriverBase：使用 PWM 通道 + IN/OUT 引脚控制两相步进电机
//  - 通过 sin_map.h 中的查表实现微步细分
//  - SetVoltage / SetVoltageDir 等接口由 Motor::Controller 计算后调用
// ============================================================================
#ifndef CTRL_STEP_FW_TB67H450_BASE_H
#define CTRL_STEP_FW_TB67H450_BASE_H

#include "driver_base.h"

class TB67H450Base : public DriverBase
{
public:
    explicit TB67H450Base()
    = default;

    void Init() override;

    void SetFocCurrentVector(uint32_t _directionInCount, int32_t _current_mA) override;

    void Sleep() override;

    void Brake() override;


protected:
    void SetTwoCoilsCurrent(uint16_t _currentA_3300mAIn12Bits, uint16_t _currentB_3300mAIn12Bits) override;


    /***** 需由平台实现的接口 *****/
    virtual void InitGpio() {};

    virtual void InitPwm(){};

    virtual void DacOutputVoltage(uint16_t _voltageA_3300mVIn12bits, uint16_t _voltageB_3300mVIn12bits){};

    virtual void SetInputA(bool _statusAp, bool _statusAm){};

    virtual void SetInputB(bool _statusBp, bool _statusBm){};
};

#endif
