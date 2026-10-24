// ============================================================================
//  mt6816_base.h —— MT6816 磁编码器抽象接口
// ----------------------------------------------------------------------------
//  - 定义 14 bit 绝对角度读取、校验与角度换算的公共入口
//  - 平台相关 SPI 传输由 Port/mt6816_stm32.* 实现
//  - 上层 Motor 只依赖 EncoderBase 接口，不直接访问硬件寄存器
// ============================================================================

#ifndef CTRL_STEP_FW_MT6816_H
#define CTRL_STEP_FW_MT6816_H

#include "encoder_base.h"
#include <cstdint>

class MT6816Base : public EncoderBase
{
public:
    explicit MT6816Base(uint16_t* _quickCaliDataPtr) :
        quickCaliDataPtr(_quickCaliDataPtr),
        spiRawData(SpiRawData_t{0})
    {
    }


    bool Init() override;
    uint16_t UpdateAngle() override;  // Get current rawAngle (rad)
    bool IsCalibrated() override;


private:
    typedef struct
    {
        uint16_t rawData;       // SPI raw 16bits data
        uint16_t rawAngle;      // 14bits rawAngle in rawData
        bool noMagFlag;
        bool checksumFlag;
    } SpiRawData_t;


    SpiRawData_t spiRawData;
    uint16_t* quickCaliDataPtr;
    uint16_t dataTx[2];
    uint16_t dataRx[2];
    uint8_t hCount;


    /***** Port Specified Implements *****/
    virtual void SpiInit() {};

    virtual uint16_t SpiTransmitAndRead16Bits(uint16_t _dataTx) { return 0; };

};

#endif
