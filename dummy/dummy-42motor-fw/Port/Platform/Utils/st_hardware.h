// ============================================================================
//  st_hardware.h —— STM32 平台杂项硬件辅助接口
// ----------------------------------------------------------------------------
//  - GetSerialNumber: 读取芯片唯一 ID 并压缩为 64 bit
//  - AdcGetChipTemperature: 读内部温度传感器（°C）
// ============================================================================
#ifndef CTRL_STEP_FW_ST_HARDWARE_H
#define CTRL_STEP_FW_ST_HARDWARE_H

#include <stdint-gcc.h>

#ifdef __cplusplus
extern "C" {
#endif

uint64_t GetSerialNumber();

#ifdef __cplusplus
}


#endif
#endif
