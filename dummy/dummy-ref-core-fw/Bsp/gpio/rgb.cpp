#include "rgb.hpp"

// ---------------------------------------------------------------------------
// 构造函数：mode 参数预留扩展点，目前未在构造期初始化任何硬件资源。
// 真正的硬件初始化（TIM2 + DMA）由 STM32CubeMX 在 main.cpp 启动时完成。
// ---------------------------------------------------------------------------
RGB::RGB(uint8_t m) {
    // 预留初始化点（当前不需要构造期硬件初始化）
}

DMA_HandleTypeDef hdma_rgb_tim5;

void RGB::InitHardware(void)
{
    hdma_rgb_tim5.Instance = DMA1_Stream3;
    hdma_rgb_tim5.Init.Channel = DMA_CHANNEL_6;
    hdma_rgb_tim5.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_rgb_tim5.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_rgb_tim5.Init.MemInc = DMA_MINC_ENABLE;
    hdma_rgb_tim5.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_rgb_tim5.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_rgb_tim5.Init.Mode = DMA_NORMAL;
    hdma_rgb_tim5.Init.Priority = DMA_PRIORITY_HIGH;
    hdma_rgb_tim5.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_rgb_tim5) != HAL_OK) Error_Handler();
    __HAL_LINKDMA(&htim5, hdma[TIM_DMA_ID_CC4], hdma_rgb_tim5);
}

extern "C" void RGB_DMA_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_rgb_tim5);
}

// ---------------------------------------------------------------------------
// Run —— 主调度入口。由 ThreadRGBUpdate 每 30 ms 调用一次。
// 根据 mode（来自 dummy.GetRGBMode()）分发到对应灯效函数：
//   - 0~10 = 用户直接选择的固定模式
//   - 11   = 自动模式，进一步根据 autoSubState 选效果（待机/工作/完成/急停/暂停）
//   - 100+ = 内部子效果（兼容直接传 _AUTO_xxx 的旧调用路径）
// ---------------------------------------------------------------------------
void RGB::Run(RGB::Rgb_style_t mode) {
    static int previousMode = -1;
    static int previousState = -1;
    if (previousMode != mode || previousState != autoSubState) {
        ResetEffectSteps();
        previousMode = mode;
        previousState = autoSubState;
    }
    switch (mode) {
        // 0 = 关闭
        case MODE_OFF:
            SolidColor(0, 0, 0);
            break;
        // 1~7 = 纯色（红橙黄绿青蓝紫）
        case MODE_RED:
        case MODE_ORANGE:
        case MODE_YELLOW:
        case MODE_GREEN:
        case MODE_CYAN:
        case MODE_BLUE:
        case MODE_PURPLE:
            PureColor(mode);
            break;
        // 8 = 自定义（用户指定纯色 RGB）
        case MODE_CUSTOM:
            SolidColor(customR, customG, customB);
            break;
        // 9 = 彩虹流水
        case MODE_RAINBOW:
            RainBow();
            break;
        // 10 = 呼吸灯（白色）
        case MODE_BREATH:
            BreathEffect(100, 100, 100, 120);
            break;
        // 11 = 自动模式 → 根据 autoSubState 分发
        case MODE_AUTO:
            switch (autoSubState) {
                case AUTO_STANDBY:
                    BreathEffect(100, 100, 100, 120);   // 白色呼吸灯
                    break;
                case AUTO_WORKING:
                    SolidColor(0, 100, 100);             // 青色常亮
                    break;
                case AUTO_COMPLETE:
                    SlowFlash(0, 100, 0);                // 绿色慢闪
                    break;
                case AUTO_ESTOP:
                    LinearFlash(100, 0, 0, 30);          // 红色线性快闪
                    break;
                case AUTO_PAUSED:
                    YellowFlow();                        // 黄色流水灯
                    break;
                default:
                    BreathEffect(100, 100, 100, 120);
                    break;
            }
            break;
        // ---- 内部子效果直接调用 ----
        case _AUTO_STANDBY:
            BreathEffect(100, 100, 100, 120);
            break;
        case _AUTO_WORKING:
            SolidColor(0, 100, 100);
            break;
        case _AUTO_COMPLETE:
            SlowFlash(0, 100, 0);
            break;
        case _AUTO_ESTOP:
            LinearFlash(100, 0, 0, 30);
            break;
        case _AUTO_PAUSED:
            YellowFlow();
            break;
        default:
            SolidColor(0, 0, 0);
            break;
    }
}

// ---------------------------------------------------------------------------
// Interrupt —— DMA 完成回调入口。
// 在 stm32f4xx_it.c 的 HAL_TIM_PWM_PulseFinishedCallback 中被调用，
// 用于通知 WS2812_Send 等待循环：本次 PWM 序列已发送完毕。
// ---------------------------------------------------------------------------
void RGB::Interrupt(uint8_t flag) {
    data_sentflag = flag;
    dmaActive = false;
}


