// ============================================================================
//  common_inc.h —— 用户应用的公共头文件汇总
// ----------------------------------------------------------------------------
//  把 main.cpp / 协议层 / 机器人对象等所需的标准头一次性 #include
//  保持各 .cpp 顶部干净；如新增大模块请将其头文件追加于此处。
// ============================================================================
#ifndef REF_STM32F4_COMMON_INC_H
#define REF_STM32F4_COMMON_INC_H

#define CONFIG_FW_VERSION 1.0    // 固件版本号，供启动横幅 / Fibre 查询

/*---------------------------- C 作用域 ---------------------------*/
// 以下头与声明在 C 与 C++ 中都可见，供 CubeMX 生成的 freertos.c 等调用
#ifdef __cplusplus
extern "C" {
#endif


#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"
#include "freertos_inc.h"
#include "adc.h"
#include "tim.h"
#include "time_utils.h"

// 由 freertos.c 的 StartDefaultTask 调用，是本项目用户主入口
void Main(void);

extern uint64_t serialNumber;        // 64 bit 板序列号（从芯片唯一 ID 读取）
extern char serialNumberStr[13];     // 十二位十六进制字符串表示，用于 USB 描述符
//extern uint64_t myserial;

#ifdef __cplusplus
}

/*---------------------------- C++ 作用域 ---------------------------*/
// 仅 C++ 部分可见。下面这些头几乎是机器人主体业务的全部拼凑：
//   - 通信层 / EEPROM
//   - OLED (U8g2) 与 IMU (MPU6050) 驱动
//   - 编码器 / ADC / PWM / WS2812 / TIM
//   - 执行器（mintasca SCA、步进 CtrlStep）
//   - 机器人门面对象
#include "communication.hpp"
#include "eeprom_interface.h"
#include "U8g2lib.hpp"
#include "MPU6050.hpp"
#include "encoder.hpp"
#include "analog.hpp"
#include "pwm.hpp"
#include "rgb.hpp"
#include "timer.hpp"
#include "actuators/mintasca/sca.hpp"
#include "actuators/ctrl_step/ctrl_step.hpp"
#include "instances/dummy_robot.h"


#endif // __cplusplus
#endif //REF_STM32F4_COMMON_INC_H —— 头文件包含守卫结束
