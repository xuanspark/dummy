#ifndef REF_STM32F4_RGB_H
#define REF_STM32F4_RGB_H

#include <cstdint>
#include <tim.h>
#include <math.h>

#define MAX_LED 12               // 板载 WS2812 灯珠数量（REF 1.0 板）
#define USE_BRIGHTNESS 0         // 是否启用 Set_Brightness 亮度衰减（默认关闭，节省 CPU）
#define PI               3.14159265358979f

// ==========================================================================
//  RGB —— WS2812 串行 LED 灯效控制器
// --------------------------------------------------------------------------
//  - 通过 TIM2_CH4 + DMA 输出符合 WS2812 时序的 PWM 波形（pwmData[]）
//  - 提供 12 种用户可见模式 + 5 种 auto 子状态（参见 Rgb_style_t）
//  - 由 ThreadRGBUpdate (main.cpp) 每 30 ms 调用一次 Run()
//  - 灯效切换时应调 ResetEffectSteps() 复位计步器以避免相位残留
// ==========================================================================
class RGB
{
private:
    uint8_t LED_Data[MAX_LED][4];     // 每颗灯的目标 GRB 数据：[idx, G, R, B]
    uint8_t LED_Mod[MAX_LED][4];      // Set_Brightness 调整后的中间缓冲（仅 USE_BRIGHTNESS 启用时使用）
    uint32_t pwmData[(24*MAX_LED)+50];// PWM 比较值缓冲（每灯 24 bit + 末尾 30 个 0 用于 reset 间隙）
    uint16_t  effStep = 0;            // 彩虹/Blink/Fade/YellowFlow 共用的相位计步器
    uint16_t  breathStep = 0;         // 呼吸灯独立计步（与 effStep 分离，避免互相干扰）
    uint16_t  flashStep = 0;          // 急停快闪/慢闪独立计步
public:
    uint8_t  data_sentflag = 0;       // DMA 完成中断置 1，主循环轮询等待发送结束

    // ────────── 灯光模式编号（持久化到 EEPROM）──────────
    //  0  = 关闭
    //  1~7= 纯色（红橙黄绿青蓝紫）
    //  8  = 自定义（纯色，用户指定 RGB）
    //  9  = 彩虹流水
    // 10  = 呼吸灯（白色）
    // 11  = 自动模式（由 autoSubState 驱动）
    enum Rgb_style_t
    {
        MODE_OFF      = 0,
        MODE_RED      = 1,
        MODE_ORANGE   = 2,
        MODE_YELLOW   = 3,
        MODE_GREEN    = 4,
        MODE_CYAN     = 5,
        MODE_BLUE     = 6,
        MODE_PURPLE   = 7,
        MODE_CUSTOM   = 8,
        MODE_RAINBOW  = 9,
        MODE_BREATH   = 10,
        MODE_AUTO     = 11,
        // ---- 以下为自动模式子效果，不暴露给用户 ----
        _AUTO_STANDBY       = 100,  // 白色呼吸灯
        _AUTO_WORKING       = 101,  // 绿色常亮
        _AUTO_COMPLETE      = 102,  // 青色慢闪（短周期呼吸）
        _AUTO_ESTOP         = 103,  // 红色线性快闪
        _AUTO_PAUSED        = 104,  // 黄色流水灯（彩虹变体）
    };

    // 自动模式的子状态标识
    enum AutoSubState
    {
        AUTO_STANDBY  = 1,  // 待机
        AUTO_WORKING  = 2,  // 工作
        AUTO_COMPLETE = 3,  // 完成
        AUTO_ESTOP    = 4,  // 急停
        AUTO_PAUSED   = 5,  // 暂停
    };

    volatile AutoSubState autoSubState = AUTO_STANDBY;

    // 自定义模式的纯色 RGB（通过 !RGB_CUSTOM r g b 设置）
    uint8_t customR = 100, customG = 100, customB = 100;

    // 构造函数：mode 参数预留，目前未使用（实际模式由 dummy.GetRGBMode() 决定）
    RGB(uint8_t mode=0);

    // 主入口：按 _mode 分发到具体灯效。由 ThreadRGBUpdate 每 30 ms 调用
    void Run(Rgb_style_t _mode = MODE_RAINBOW);

    // DMA 完成回调（HAL_TIM_PWM_PulseFinishedCallback 内调用），置 data_sentflag
    void Interrupt(uint8_t flag);

    // 切换效果时重置所有计步器，避免残留相位导致视觉跳变
    void ResetEffectSteps() { effStep = 0; breathStep = 0; flashStep = 0; }

    //functions
    // 写入第 LEDs 颗灯的 RGB 值到内部缓冲（不立即发送，等 WS2812_Send 一次性 DMA 推出）。
    // 注意 WS2812 实际线序是 GRB，所以存储顺序是 [idx, G, R, B]。
    void Set_LED (uint8_t LEDs, uint8_t Red, uint8_t Green, uint8_t Blue)
    {
        LED_Data[LEDs][0] = LEDs;
        LED_Data[LEDs][1] = Green;
        LED_Data[LEDs][2] = Red;
        LED_Data[LEDs][3] = Blue;
    }

    // 全局亮度衰减（0~45）。当前默认 USE_BRIGHTNESS=0 编译时整个函数体为空，
    // 所有灯效直接以 100/255 等小幅亮度写入避免功耗与色偏（WS2812 高亮易偏色）。
    void Set_Brightness (int brightness)  // 0-45
    {
#if USE_BRIGHTNESS

        if (brightness > 45) brightness = 45;
        for (int i=0; i<MAX_LED; i++)
        {
            LED_Mod[i][0] = LED_Data[i][0];
            for (int j=1; j<4; j++)
            {
                float angle = 105-brightness;  // in degrees
                angle = angle*PI / 180;  // in rad
                LED_Mod[i][j] = (LED_Data[i][j])/(tan(angle));
            }
        }

#endif

    }

    // 把 LED_Data 序列化为 WS2812 时序的 PWM 比较值并通过 DMA 发送。
    // WS2812 协议：每 bit 由一段 PWM 高电平时长决定（0:1/3 周期，1:2/3 周期），
    // 末尾 50 µs 低电平作为 reset 间隙。这里 PWM 周期 = 105 计数（70 = 2/3，30 = 1/3）。
    // DMA 启动后阻塞等待 data_sentflag（由 PulseFinished 回调置 1）。
    void WS2812_Send (void)
    {
        uint32_t indx=0;
        uint32_t color;
        HAL_StatusTypeDef ret;

        for (int i= 0; i<MAX_LED; i++)
        {
#if USE_BRIGHTNESS
            color = ((LED_Mod[i][1]<<16) | (LED_Mod[i][2]<<8) | (LED_Mod[i][3]));
#else
            color = ((LED_Data[i][1]<<16) | (LED_Data[i][2]<<8) | (LED_Data[i][3]));
#endif
            for (int i=23; i>=0; i--)
            {
                // 比特 1 的时序
                if (color&(1<<i))
                {
                    pwmData[indx] = 70;  // 高电平占 2/3 周期 (105 * 2/3 = 70)
                }
                // 比特 0 的时序
                else pwmData[indx] = 30;  // 高电平占 1/3 周期 (105 * 1/3 = 30)
                indx++;
            }

        }

        // 在两个数据周期之间填 50 µs 低电平作为 WS2812 复位间隙
        for (int i=0; i<30; i++)
        {
            pwmData[indx] = 0;
            indx++;
        }

        ret = HAL_TIM_PWM_Start_DMA(&htim2, TIM_CHANNEL_4, pwmData, indx);
        if (ret != HAL_OK) {
            data_sentflag = 0;
            return;
        }
        while (!data_sentflag){};
        data_sentflag = 0;
    }

    // 彩虹流水：12 个 step 一周期，每颗灯按位置偏移取色相，颜色平滑过渡。
    // 用两个相邻颜色的线性插值（factor1/factor2）模拟 HSV 旋转。
    void RainBow()
    {
        float factor1, factor2;
        uint16_t ind;
        for(uint16_t j=0;j<MAX_LED;j++) {
            ind = effStep + j * 1;
            switch((int)((ind % 12) / 4)) {
                case 0: factor1 = 1.0 - ((float)(ind % 12 - 0 * 4) / 4);
                    factor2 = (float)((int)(ind - 0) % 12) / 4;
                    Set_LED(j, 255 * factor1 + 0 * factor2, 0 * factor1 + 255 * factor2, 0 * factor1 + 0 * factor2);
                    break;
                case 1: factor1 = 1.0 - ((float)(ind % 12 - 1 * 4) / 4);
                    factor2 = (float)((int)(ind - 4) % 12) / 4;
                    Set_LED(j, 0 * factor1 + 0 * factor2, 255 * factor1 + 0 * factor2, 0 * factor1 + 255 * factor2);
                    break;
                case 2: factor1 = 1.0 - ((float)(ind % 12 - 2 * 4) / 4);
                    factor2 = (float)((int)(ind - 8) % 12) / 4;
                    Set_LED(j, 0 * factor1 + 255 * factor2, 0 * factor1 + 0 * factor2, 255 * factor1 + 0 * factor2);
                    break;
            }
        }
        WS2812_Send();  // 所有 LED 设置完后再发送
        if(effStep >= 12) {effStep = 0; }
        else effStep++;
    }

    // 预留：用户可在此处自定义颜色序列。当前空实现。
    void Sequence() {
        //在这里自定义颜色序列
    }

    // 老版闪烁效果（保留为参考）：先暗 44 帧 → 渐亮 → 全亮 → 渐暗 → 全暗，
    // 共 75.6 帧一周期。当前 12 个用户模式中已不直接使用，被新的 BreathEffect/LinearFlash 替代。
    void Blink()
    {
        uint8_t e,r,g,b;
        if(effStep < 44) {
            for(uint16_t j=0;j<MAX_LED;j++)
            {
                Set_LED(j, 0, 0, 0);
            }
        }
        else if(effStep  < 46) {
            e = (effStep * 5) - 220;
            r = 255 * ( e / 10 ) + 0 * ( 1.0 - e / 10 );
            g = 255 * ( e / 10 ) + 0 * ( 1.0 - e / 10 );
            b = 255 * ( e / 10 ) + 0 * ( 1.0 - e / 10 );
            for(uint16_t j=0;j<12;j++)
                if((j%1)==0)
                {
                    Set_LED(j, r, g, b);
                }
                else
                {
                    Set_LED(j, 0, 0, 0);
                }
        }
        else if(effStep < 53.6) {
            for(uint16_t j=0;j<MAX_LED;j++)
                if((j%1)==0)
                {
                    Set_LED(j, 255, 255, 255);
                }
                else
                {
                    Set_LED(j, 0, 0, 0);
                }
        }
        else if(effStep < 55.6) {
            e = (effStep * 5) - 268;
            r = 0 * ( e / 10 ) + 255 * ( 1.0 - e / 10 );
            g = 0 * ( e / 10 ) + 255 * ( 1.0 - e / 10 );
            b = 0 * ( e / 10 ) + 255 * ( 1.0 - e / 10 );
            for(uint16_t j=0;j<MAX_LED;j++)
                if((j%1)==0)
                {
                    Set_LED(j, r, g, b);
                }
                else
                {
                    Set_LED(j, 0, 0, 0);
                }
        }
        else {
            for(uint16_t j=0;j<MAX_LED;j++)
            {
                Set_LED(j, 0, 0, 0);
            }
        }
        WS2812_Send();  // 所有 LED 设置完后再发送
        if(effStep >= 75.6) {effStep = 0; }
        else effStep++;
    }

    // 老版渐亮效果（保留为参考）：35.15 帧内从黑渐亮到全白，单向。
    // 当前 12 个用户模式中已不直接使用。
    void Fade() {
        uint8_t r,g,b;
        double e;
        e = (effStep * 20) / (double)703;
        r = ( e ) * 255 + 0 * ( 1.0 - e );
        g = ( e ) * 255 + 0 * ( 1.0 - e );
        b = ( e ) * 255 + 0 * ( 1.0 - e );
        for(uint16_t j=0;j<MAX_LED;j++) {
            if((j % 1) == 0)
            {
                Set_LED(j, r, g, b);
            }
            else
            {
                Set_LED(j, 0, 0, 0);
            }
        }
        WS2812_Send();  // 所有 LED 设置完后再发送
        if(effStep >= 35.15) {effStep = 0; }
        else effStep++;
    }

    // 通用纯色：按 RGB 值全部填充
    void SolidColor(uint8_t r, uint8_t g, uint8_t b) {
        for(uint16_t j=0;j<MAX_LED;j++) {
            Set_LED(j, r, g, b);
        }
        WS2812_Send();
    }

    void PureColor(Rgb_style_t color) {
        switch (color){
            case MODE_RED:     SolidColor(100, 0, 0);     break;
            case MODE_ORANGE:  SolidColor(100, 40, 0);    break;
            case MODE_YELLOW:  SolidColor(100, 100, 0);   break;
            case MODE_GREEN:   SolidColor(0, 100, 0);     break;
            case MODE_CYAN:    SolidColor(0, 100, 100);   break;
            case MODE_BLUE:    SolidColor(0, 0, 100);     break;
            case MODE_PURPLE:  SolidColor(60, 0, 100);    break;
            default:           SolidColor(0, 0, 0);       break;
        }
    }

    // 呼吸灯效果（可指定颜色 RGB，周期由 period 控制，period 越大越慢）
    void BreathEffect(uint8_t r, uint8_t g, uint8_t b, uint16_t period = 120) {
        // 正弦曲线亮度：0 → 1 → 0
        float phase = (float)breathStep / (float)period;
        float brightness = (sinf(phase * 2.0f * PI - PI / 2.0f) + 1.0f) / 2.0f;
        uint8_t rr = (uint8_t)(r * brightness);
        uint8_t gg = (uint8_t)(g * brightness);
        uint8_t bb = (uint8_t)(b * brightness);
        SolidColor(rr, gg, bb);
        breathStep++;
        if (breathStep >= period) breathStep = 0;
    }

    // 线性快闪（急停用）：线性三角波，不是突然开关
    void LinearFlash(uint8_t r, uint8_t g, uint8_t b, uint16_t period = 30) {
        float phase = (float)flashStep / (float)period;
        // 三角波：0→1→0 线性
        float brightness;
        if (phase < 0.5f)
            brightness = phase * 2.0f;
        else
            brightness = 2.0f - phase * 2.0f;
        uint8_t rr = (uint8_t)(r * brightness);
        uint8_t gg = (uint8_t)(g * brightness);
        uint8_t bb = (uint8_t)(b * brightness);
        SolidColor(rr, gg, bb);
        flashStep++;
        if (flashStep >= period) flashStep = 0;
    }

    // 慢闪（完成状态用）：复用呼吸灯，周期更短以区分待机
    void SlowFlash(uint8_t r, uint8_t g, uint8_t b) {
        BreathEffect(r, g, b, 60);  // 周期 60 < 呼吸灯 120
    }

    // 黄色流水灯（暂停用）：复用彩虹逻辑但限定为黄色系
    void YellowFlow() {
        for(uint16_t j=0; j<MAX_LED; j++) {
            uint16_t ind = (effStep + j) % MAX_LED;
            // 只有一颗灯高亮黄色，其余暗黄
            if (ind == 0) {
                Set_LED(j, 120, 100, 0);
            } else if (ind == 1 || ind == MAX_LED - 1) {
                Set_LED(j, 50, 40, 0);
            } else {
                Set_LED(j, 10, 8, 0);
            }
        }
        WS2812_Send();
        effStep++;
        if (effStep >= MAX_LED) effStep = 0;
    }

};

#endif //REF_STM32F4_RGB_H

