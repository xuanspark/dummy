// ============================================================================
//  mt6816_stm32.cpp —— MT6816 SPI 读位置实现（HAL SPI1）
// ----------------------------------------------------------------------------
//  - SPI 命令 0x8300 + 2 字节回读 = 14 bit 角度 + 2 bit 状态
//  - DMA 接收方式以减少占用，UpdateAngle 内查校准表得到校正角
// ============================================================================
#include "mt6816_stm32.h"
#include "spi.h"

void MT6816::SpiInit()
{
    MX_SPI1_Init();
}

uint16_t MT6816::SpiTransmitAndRead16Bits(uint16_t _dataTx)
{
    uint16_t dataRx;

    GPIOA->BRR = GPIO_PIN_15; // Chip select
    HAL_SPI_TransmitReceive(&hspi1, (uint8_t*) &_dataTx, (uint8_t*) &dataRx, 1, HAL_MAX_DELAY);
    GPIOA->BSRR = GPIO_PIN_15;

    return dataRx;
}
