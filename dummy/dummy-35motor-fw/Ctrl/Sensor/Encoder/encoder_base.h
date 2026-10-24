// ============================================================================
//  encoder_base.h —— 编码器抽象基类（35 步进版本）
// ----------------------------------------------------------------------------
//  - 与 42motor 镜像。派生类 MT6816 实现具体磁编码器读
// ============================================================================
#ifndef CTRL_STEP_FW_ENCODER_H
#define CTRL_STEP_FW_ENCODER_H

#include <cstdint>


class EncoderBase
{
public:
    typedef struct
    {
        uint16_t rawAngle;          // raw data
        uint16_t rectifiedAngle;    // calibrated rawAngle data
        bool rectifyValid;
    } AngleData_t;
    AngleData_t angleData{0};


    /*
     * 分辨率 = 2^14 = 16384，每个状态需一个 uint16 作为映射，
     * 因此校准表所需 Flash 总量 = 32 KB。
    */
    const int32_t RESOLUTION = ((int32_t) ((0x00000001U) << 14));

    virtual bool Init() = 0;

    // 读当前原始角度（未经校准）
    virtual uint16_t UpdateAngle() = 0;

    virtual bool IsCalibrated() = 0;


private:

};

#endif
