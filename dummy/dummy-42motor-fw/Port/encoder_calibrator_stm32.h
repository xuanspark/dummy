// ============================================================================
//  encoder_calibrator_stm32.h —— EncoderCalibratorBase 在 STM32 平台的实现
// ----------------------------------------------------------------------------
//  - 校准结果（一圈 16384 个原始计数 → 校正表）写入 STM32F103CB Flash 末页
//  - 使用 stockpile_f103cb 提供的 Flash 抹写接口
// ============================================================================
#ifndef CTRL_STEP_FW_ENCODER_CALIBRATOR_H
#define CTRL_STEP_FW_ENCODER_CALIBRATOR_H

#include "Sensor/Encoder/encoder_calibrator_base.h"

class EncoderCalibrator : public EncoderCalibratorBase
{
public:
    explicit EncoderCalibrator(Motor* _motor) : EncoderCalibratorBase(_motor)
    {}


private:
    void BeginWriteFlash() override;
    void EndWriteFlash() override;
    void ClearFlash() override;
    void WriteFlash16bitsAppend(uint16_t _data) override;
};

#endif
