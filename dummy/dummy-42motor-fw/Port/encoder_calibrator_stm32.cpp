// ============================================================================
//  encoder_calibrator_stm32.cpp —— EncoderCalibrator STM32 实现
// ----------------------------------------------------------------------------
//  - 调用 stockpile 抹写 Flash 末页保存校准表
//  - 通过 SPI 周期性读 MT6816 角度求平均得到校正映射
// ============================================================================
#include "encoder_calibrator_stm32.h"
#include "Platform/Memory/stockpile_f103cb.h"

void EncoderCalibrator::BeginWriteFlash()
{
    Stockpile_Flash_Data_Begin(&stockpile_quick_cali);
}


void EncoderCalibrator::EndWriteFlash()
{
    Stockpile_Flash_Data_End(&stockpile_quick_cali);
}


void EncoderCalibrator::ClearFlash()
{
    Stockpile_Flash_Data_Empty(&stockpile_quick_cali);
}


void EncoderCalibrator::WriteFlash16bitsAppend(uint16_t _data)
{
    Stockpile_Flash_Data_Write_Data16(&stockpile_quick_cali, &_data, 1);
}
