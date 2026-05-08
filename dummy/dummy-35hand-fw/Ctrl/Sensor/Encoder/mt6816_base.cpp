// ============================================================================
//  mt6816_base.cpp —— MT6816 抽象基类实现（35 步进版本，与 42motor 镜像）
// ============================================================================
#include "mt6816_base.h"

bool MT6816Base::Init()
{
    SpiInit();
    UpdateAngle();

    // 检查 Flash 中存的校准数据是否有效
    angleData.rectifyValid = true;
    for (uint32_t i = 0; i < RESOLUTION; i++)
    {
        if (quickCaliDataPtr[i] == 0xFFFF)
            angleData.rectifyValid = false;
    }

    return angleData.rectifyValid;
}


uint16_t MT6816Base::UpdateAngle()
{
    dataTx[0] = (0x80 | 0x03) << 8;
    dataTx[1] = (0x80 | 0x04) << 8;

    for (uint8_t i = 0; i < 3; i++)
    {
        dataRx[0] = SpiTransmitAndRead16Bits(dataTx[0]);
        dataRx[1] = SpiTransmitAndRead16Bits(dataTx[1]);

        spiRawData.rawData = ((dataRx[0] & 0x00FF) << 8) | (dataRx[1] & 0x00FF);

        //奇偶校验
        hCount = 0;
        for (uint8_t j = 0; j < 16; j++)
        {
            if (spiRawData.rawData & (0x0001 << j))
                hCount++;
        }
        if (hCount & 0x01)
        {
            spiRawData.checksumFlag = false;
        } else
        {
            spiRawData.checksumFlag = true;
            break;
        }
    }

    if (spiRawData.checksumFlag)
    {
        spiRawData.rawAngle = spiRawData.rawData >> 2;
        spiRawData.noMagFlag = (bool) (spiRawData.rawData & (0x0001 << 1));
    }

    angleData.rawAngle = spiRawData.rawAngle;
    angleData.rectifiedAngle = quickCaliDataPtr[angleData.rawAngle];

    return angleData.rectifiedAngle;
}


bool MT6816Base::IsCalibrated()
{
    return angleData.rectifyValid;
}

