// ============================================================================
//  encoder.cpp —— Encoder 类实现：TIM 编码器读 + 累计 + 速度差分
// ============================================================================
#include "encoder.hpp"

// 构造函数仅保存参数。真正启动 TIM 编码器需后续调 Start()。
Encoder::Encoder(TIM_HandleTypeDef *_htim, uint16_t _cpr, bool _inverse) :
    htim(_htim), config(Config_t{})
{
    config.cpr = _cpr;
    config.inverse = _inverse;
}

// 16 bit TIM 计数器 + 软件高位累加 (GetCntLoop) 拼成 64 bit 位置。
// 反转标记开启时取负，避免上层软件重复处理方向。
int64_t Encoder::GetCount()
{
    int64_t count = GetCntLoop(htim->Instance) * 65536 + htim->Instance->CNT;

    return config.inverse ? -count : count;
}

// 计数 / cpr 得到转数，再依需以度 / 弧度输出。未调 Start 前结果为 0。
float Encoder::GetAngle(bool _useRAD)
{
    float angle = (float) GetCount() / (float) config.cpr;

    return _useRAD ? angle / RAD_TO_DEG : angle;
}

// 启动 TIM 编码器与其中断：
//   1. ClearCntLoop：清零软件高位记载器
//   2. CNT=0：计数器零点对齐
//   3. HAL_TIM_Encoder_Start_IT：启动硬件计数 + 中断（为高位加中转表示服务）
void Encoder::Start()
{
    ClearCntLoop(htim->Instance);
    htim->Instance->CNT = 0;
    HAL_TIM_Encoder_Start_IT(htim, TIM_CHANNEL_ALL);
}
