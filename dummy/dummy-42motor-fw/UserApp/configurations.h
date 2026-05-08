// ============================================================================
//  configurations.h —— 本板配置（EEPROM 保存、上电加载）
// ----------------------------------------------------------------------------
//  - BoardConfig_t 是存到 EEPROM 的唯一结构体。main.cpp 启动时读，
//    处理 ASCII / CAN 修改后置 configStatus=COMMIT 让主循环制入。
//  - configStatus 三状态机：RESTORE → 写回后重启、OK → 平安、COMMIT → 提交
// ============================================================================
#ifndef CONFIGURATIONS_H
#define CONFIGURATIONS_H

#ifdef __cplusplus
extern "C" {
#endif
/*---------------------------- C 作用域 ---------------------------*/
#include <stdbool.h>
#include "stdint-gcc.h"

// 配置状态。主循环轮询 boardConfig.configStatus，按类型决定动作。
typedef enum configStatus_t
{
    CONFIG_RESTORE = 0,    // 需写回 EEPROM 后立即重启令生效
    CONFIG_OK,             // 正常运行、无待交下内容
    CONFIG_COMMIT          // 已修改、需下一轮主循环提交
} configStatus_t;


// 本板配置。以 EEPROM 偏移 0 处以原生字节序列化保存。
typedef struct Config_t
{
    configStatus_t configStatus;        // 状态机（见上面枚举）
    uint32_t canNodeId;                 // CAN 节点 ID（实际以拨码开关为准，这里仅备份）
    int32_t encoderHomeOffset;          // 编码器零点偏移（出厂校准得出）
    uint32_t defaultMode;               // 上电默认运行模式（Motor::Mode_t）
    int32_t currentLimit;               // 额定电流限制 (mA)
    int32_t velocityLimit;              // 额定速度限制（子步计数 / 秒）
    int32_t velocityAcc;                // 额定加速度（子步计数 / 秒²）
    int32_t calibrationCurrent;         // 编码器校准过程使用的电流 (mA)
    int32_t dce_kp;                     // DCE 位置环比例增益
    int32_t dce_kv;                     // DCE 速度环比例增益
    int32_t dce_ki;                     // DCE 积分增益
    int32_t dce_kd;                     // DCE 微分增益
    float motor_temperature;            // 当前电机温度（°C，Tim1Callback100Hz 采样上报）
    bool enableMotorOnBoot;             // 上电是否自动使能
    bool enableStallProtect;            // 是否启用堆转保护
    bool enableTempWatch;               // 是否启用温度采样 / 报警
} BoardConfig_t;

extern BoardConfig_t boardConfig;       // 全局唯一实例，定义在 main.cpp


#ifdef __cplusplus
}
/*---------------------------- C++ 作用域 ---------------------------*/
// 在 C++ 上下文依赖 EEPROM 与 Motor 定义（供 BoardConfig_t 与 Motor::Mode_t 互运）
#include <Platform/Memory/eeprom_interface.h>
#include "Motor/motor.h"


#endif
#endif // CONFIGURATIONS_H —— 头文件包含守卫结束
