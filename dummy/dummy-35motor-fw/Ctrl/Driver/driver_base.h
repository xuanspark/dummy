// ============================================================================
//  driver_base.h —— 步进电机驱动抽象基类（35 步进版本）
// ----------------------------------------------------------------------------
//  - 与 42motor 镜像，派生类（如 TB67H450Base）实现具体 H 桥控制
// ============================================================================
#ifndef CTRL_STEP_FW_DRIVER_BASE_H
#define CTRL_STEP_FW_DRIVER_BASE_H

#include <cstdint>


class DriverBase
{
public:
    virtual void Init() = 0;

    /*
     * FOC 电流矢量方向以计数表示，涵义如下：
     * we divide a 360° circle into N counts, and _directionInCount is
     * 取值范围 (0 ~ N-1)，计算后电流范围为 (0 ~ 3300 mA)
     */
    virtual void SetFocCurrentVector(uint32_t _directionInCount, int32_t _current_mA) = 0;

    virtual void Sleep() = 0;

    virtual void Brake() = 0;


protected:
    // 合成 FOC 电流矢量（主接口）
    virtual void SetTwoCoilsCurrent(uint16_t _currentA_mA, uint16_t _currentB_mA) = 0;

    typedef struct
    {
        uint16_t sinMapPtr;
        int16_t sinMapData;
        uint16_t dacValue12Bits;
    } FastSinToDac_t;

    FastSinToDac_t phaseB{};
    FastSinToDac_t phaseA{};
};

#endif
