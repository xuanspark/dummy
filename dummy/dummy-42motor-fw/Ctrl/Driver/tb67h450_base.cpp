// ============================================================================
//  tb67h450_base.cpp —— TB67H450 抽象基类实现
// ----------------------------------------------------------------------------
//  - 实现 DriverBase 接口的核心运算（电压设定、方向、PWM 占空比换算）
//  - 实际 GPIO/PWM 操作在派生类 TB67H450 (port/tb67h450_stm32) 中完成
// ============================================================================
#include "tb67h450_base.h"
#include "sin_map.h"

void TB67H450Base::Init()
{
    InitGpio();
    InitPwm();
}

void TB67H450Base::SetFocCurrentVector(uint32_t _directionInCount, int32_t _current_mA)
{
    phaseB.sinMapPtr = (_directionInCount) & (0x000003FF);
    phaseA.sinMapPtr = (phaseB.sinMapPtr + (256)) & (0x000003FF);

    phaseA.sinMapData = sin_pi_m2[phaseA.sinMapPtr];
    phaseB.sinMapData = sin_pi_m2[phaseB.sinMapPtr];

    uint32_t dac_reg = abs(_current_mA);
    dac_reg = (uint32_t) (dac_reg * 5083) >> 12;
    dac_reg = dac_reg & (0x00000FFF);
    phaseA.dacValue12Bits =
        (uint32_t) (dac_reg * abs(phaseA.sinMapData)) >> sin_pi_m2_dpiybit;
    phaseB.dacValue12Bits =
        (uint32_t) (dac_reg * abs(phaseB.sinMapData)) >> sin_pi_m2_dpiybit;

    SetTwoCoilsCurrent(phaseA.dacValue12Bits, phaseB.dacValue12Bits);

    if (phaseA.sinMapData > 0)
        SetInputA(true, false);
    else if (phaseA.sinMapData < 0)
        SetInputA(false, true);
    else
        SetInputA(true, true);

    if (phaseB.sinMapData > 0)
        SetInputB(true, false);
    else if (phaseB.sinMapData < 0)
        SetInputB(false, true);
    else
        SetInputB(true, true);
}


void TB67H450Base::SetTwoCoilsCurrent(uint16_t _currentA_3300mAIn12Bits, uint16_t _currentB_3300mAIn12Bits)
{
    /*
     * SetFocCurrentVector 计算后，12 bit 值映射为 0~3300 mA。
     * 由于需使用 0.1 Ω 采样电阻，0~3300 mV 参考即表 0~3300 mA 设点。
     * 详见 TB67H450 数据手册 P10。
     */

    DacOutputVoltage(_currentA_3300mAIn12Bits, _currentB_3300mAIn12Bits);
}


void TB67H450Base::Sleep()
{
    phaseA.dacValue12Bits = 0;
    phaseB.dacValue12Bits = 0;

    SetTwoCoilsCurrent(phaseA.dacValue12Bits, phaseB.dacValue12Bits);

    SetInputA(false, false);
    SetInputB(false, false);
}


void TB67H450Base::Brake()
{
    phaseA.dacValue12Bits = 0;
    phaseB.dacValue12Bits = 0;

    SetTwoCoilsCurrent(phaseA.dacValue12Bits, phaseB.dacValue12Bits);

    SetInputA(true, true);
    SetInputB(true, true);
}


