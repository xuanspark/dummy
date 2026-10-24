#ifndef CTRL_STEP_FW_MT6816_STM32_H
#define CTRL_STEP_FW_MT6816_STM32_H

#include "Sensor/Encoder/mt6816_base.h"

class MT6816 : public MT6816Base
{
public:
    /*
     * _quickCaliDataPtr 是校准数据在 Flash 中的起始地址。
     * STM32F103CBT6 Flash 大小为 128 KB (0x08000000 ~ 0x08020000)，
     * 末尾 33 KB（32 KB 校准表 + 1 KB 用户区）用于存储，起始地址 0x08017C00。
     */
    explicit MT6816() : MT6816Base((uint16_t*) (0x08017C00))
    {}

private:
    void SpiInit() override;

    uint16_t SpiTransmitAndRead16Bits(uint16_t _data) override;
};

#endif
