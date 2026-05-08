// ============================================================================
//  analog.cpp —— Analog 类实现：内部温度传感器读取等
// ============================================================================
#include "analog.hpp"

// 读内部温度：实际采样与公式转换在 CubeMX 生成的 AdcGetChipTemperature() 中。
float Analog::GetChipTemperature()
{
    return AdcGetChipTemperature();
}

// 按枚举分发到 CubeMX 定义的 ADC_CH1..CH4，返回伏特。
float Analog::GetVoltage(Analog::AdcChannel_t _channel)
{
    switch (_channel)
    {
        case CH1:
            return AdcGetVoltage(ADC_CH1);
        case CH2:
            return AdcGetVoltage(ADC_CH2);
        case CH3:
            return AdcGetVoltage(ADC_CH3);
        case CH4:
            return AdcGetVoltage(ADC_CH4);
    }

    return 0;
}

// 返回原始 12 bit ADC 计数（0~4095）。需为伏特请调 GetVoltage。
uint16_t Analog::GetRaw(Analog::AdcChannel_t _channel)
{
    switch (_channel)
    {
        case CH1:
            return AdcGetRaw(ADC_CH1);
        case CH2:
            return AdcGetRaw(ADC_CH2);
        case CH3:
            return AdcGetRaw(ADC_CH3);
        case CH4:
            return AdcGetRaw(ADC_CH4);
    }

    return 0;
}
