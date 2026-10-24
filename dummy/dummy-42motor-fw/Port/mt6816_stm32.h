// ============================================================================
//  mt6816_stm32.h —— MT6816Base 在 STM32 平台的实现
// ----------------------------------------------------------------------------
//  - 派生 MT6816Base：通过 HAL SPI 1 与编码器通信
//  - 校准表 quickCaliData 来自 stockpile_f103cb 的 Flash 持久化区域
// ============================================================================
#ifndef CTRL_STEP_FW_MT6816_STM32_H
#define CTRL_STEP_FW_MT6816_STM32_H

#include "Sensor/Encoder/mt6816_base.h"

class MT6816 : public MT6816Base
{
public:
    /*
     * _quickCaliDataPtr 是校准数据在 Flash 中的起始地址，
     * STM32F103CBT6 共 Flash 128 KB (0x08000000 ~ 0x08020000)，本项目使用
     * 最后 33 KB（32 KB 校准 + 1 KB 用户区），起始地址 0x08017C00。
     */
    explicit MT6816() : MT6816Base((uint16_t*) (0x08017C00))
    {}

private:
    void SpiInit() override;

    uint16_t SpiTransmitAndRead16Bits(uint16_t _data) override;
};

#endif
