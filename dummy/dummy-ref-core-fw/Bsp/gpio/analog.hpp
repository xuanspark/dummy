// ============================================================================
//  analog.hpp —— ADC 采样封装
// ----------------------------------------------------------------------------
//  - 封装内部温度传感器读取（GetChipTemperature）等模拟输入
//  - 实际 ADC 通道在 STM32CubeMX 配置
// ============================================================================
#ifndef REF_STM32F4_ANALOG_H
#define REF_STM32F4_ANALOG_H

#include <cstdint>
#include <adc.h>

class Analog
{
private:

public:
    // ADC 用户通道枚举（具体引脚与采样频率在 CubeMX 配置）
    enum AdcChannel_t
    {
        CH1,
        CH2,
        CH3,
        CH4
    };

    Analog()
    = default;

    // 读 STM32 内部温度传感器（°C）。依赖 CubeMX 中将内部温度通道使能。
    float GetChipTemperature();

    // 读指定用户通道的模拟电压（伏特）。内部使用 3.3 V 参考。
    float GetVoltage(AdcChannel_t _channel);

    // 读指定用户通道的 ADC 原始计数 (12 bit，0–4095)。
    uint16_t GetRaw(AdcChannel_t _channel);
};

#endif //REF_STM32F4_ANALOG_H
