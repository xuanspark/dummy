// ============================================================================
//  time_utils.c —— micros / millis / delayUs 实现
// ============================================================================
#include "time_utils.h"

// 读 SysTick CTRL 中的 COUNTFLAG 位：1 表示自上次读以来计数器至少已减至 0 一次。
// 重要副作用：读后该位自动清零，因此本函数兼具 "读取 + 清除" 两重作用。
__STATIC_INLINE uint32_t LL_SYSTICK_IsActiveCounterFlag(void)
{
    //判断COUNTFLAG位是否为1，1则计数器已经递减到0了至少一次。读取该位后该位自动清零。
    return ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == (SysTick_CTRL_COUNTFLAG_Msk));
}

// micros: 通过 "HAL_GetTick (毫秒) + SysTick 计数器剩余 (亚毫秒)" 拼出微秒数。
// 关键点：先读 ms 再读 SysTick→VAL 期间若发生溢出会导致两者不同步，
// 因此读完后再次检查 COUNTFLAG，命中则重读一遍以保证一致性。
uint32_t micros(void)
{
    /* 通过读 SysTick CTRL 寄存器清除 COUNTFLAG，确保下面的判断从已知态开始 */
    LL_SYSTICK_IsActiveCounterFlag();           //清除计数器"溢出"标志位
    uint32_t m = HAL_GetTick();                 // 读取毫秒计数
    const uint32_t tms = SysTick->LOAD + 1;     // SysTick 一个周期对应的滴答数（= 系统时钟 / 1000）
    __IO uint32_t u = tms - SysTick->VAL;       // 计算本次毫秒内已经过的滴答数
    if (LL_SYSTICK_IsActiveCounterFlag())
    {
        // 读期间发生了溢出（毫秒已 +1 但我们读了旧值），重新采样保证一致
        m = HAL_GetTick();
        u = tms - SysTick->VAL;
    }
    return (m * 1000 + (u * 1000) / tms);       // 拼合微秒：ms × 1000 + 当前毫秒内已过的微秒
}

// millis: HAL_GetTick 直通，仅用于命名一致性。
uint32_t millis(void)
{
    return HAL_GetTick();
}

// delayMicroseconds: 用 NOP 空循环估算微秒延时。
// us *= 23 是 168 MHz 主频经验值（每次循环约 7 周期 → 168/7 ≈ 24，取 23 留余量）。
// 不准确但开销小，仅用于不需要严格定时的短延时。
void delayMicroseconds(uint32_t us)
{
    us *= 23;
    while (us--)
        __NOP();
}

