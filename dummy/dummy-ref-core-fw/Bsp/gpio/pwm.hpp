// ============================================================================
//  pwm.hpp —— 双 TIM × 双通道 PWM 生成器
// ----------------------------------------------------------------------------
//  - 使用 htim9 / htim12，各 2 通道 → 共 4 路 PWM 输出 (CH_A1/A2/B1/B2)
//  - 频率独立设置（构造时传入 freqHzA / freqHzB）
//  - SetDuty 取值 0~1
// ============================================================================
#ifndef REF_STM32F4_PWM_H
#define REF_STM32F4_PWM_H

#include <cstdint>
#include <tim.h>

class PWM
{
private:

public:
    // 通道枚举：A 组在 htim9、B 组在 htim12。CH_ALL 代表全部 4 路同时操作。
    enum PwmChannel_t
    {
        CH_ALL,
        // TIM
        CH_A1,
        CH_A2,
        CH_B1,
        CH_B2
    };

    // 默认 21 kHz。频率在构造时定、重新设需重新构造。
    explicit PWM(uint32_t _freqHzA = 21000, uint32_t _freqHzB = 21000);

    // 启动指定通道的 PWM 输出（使能对应 CCR 输出）。
    void Start(PwmChannel_t _channel = CH_ALL);

    // _duty 取值 0~1，内部会限付后乘上 ARR 得出 CCR 。
    void SetDuty(PwmChannel_t _channel = CH_ALL, float _duty = 0);

};

#endif //REF_STM32F4_PWM_H
