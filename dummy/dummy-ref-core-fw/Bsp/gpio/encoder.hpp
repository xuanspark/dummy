// ============================================================================
//  encoder.hpp —— 正交编码器（TIM 编码器模式）封装
// ----------------------------------------------------------------------------
//  - 通过 TIM 硬件编码器接口读取脉冲计数 → 累加到 64 位有符号位置
//  - 提供 GetCount / GetSpeed 等接口；速度由相邻调用差分得到
//  - inverse 标志反转计数符号（机械装配差异时使用）
// ============================================================================
#ifndef REF_STM32F4_ENCODER_HPP
#define REF_STM32F4_ENCODER_HPP

#include <fibre/protocol.hpp>
#include "tim.h"

class Encoder
{
private:
    const float RAD_TO_DEG = 57.295777754771045f;   // 转化常量，避免调 cmath

    TIM_HandleTypeDef *htim;                         // HAL TIM 句柄（需高位为 32 bit 加中转表示足够范围）

public:
    // _cpr        每转计数（默认 4096）
    // _inverse    是否反转计数符号（机械改装背面安装时使用）
    explicit Encoder(TIM_HandleTypeDef *_htim, uint16_t _cpr = 4096, bool _inverse = false);

    // 启动 TIM 编码器模式。必须在 CubeMX 中预先启用。
    void Start();

    // 返回从 Start 后累加的位置计数（64 位有符号、考虑 inverse）。
    int64_t GetCount();

    // 返回当前角度。_useRAD=false 返回度，true 返回弧度。
    float GetAngle(bool _useRAD = false);

    // 静态参数。cpr 与 inverse 如需调参可通过 fibre 远程修改。
    struct Config_t
    {
        uint16_t cpr;

        bool inverse;
    };

    // Fibre 远程接口：PC 可读写 cpr/inverse、调用 get_count / get_angle。
    auto MakeProtocolDefinitions()
    {
        return make_protocol_member_list(
            make_protocol_object("config",
                                 make_protocol_property("cpr", &config.cpr),
                                 make_protocol_property("inverse", &config.inverse)
            ),
            make_protocol_function("get_count", *this, &Encoder::GetCount),
            make_protocol_function("get_angle", *this, &Encoder::GetAngle, "use_rad")
        );
    }

    Config_t config;
};

#endif //REF_STM32F4_ENCODER_HPP
