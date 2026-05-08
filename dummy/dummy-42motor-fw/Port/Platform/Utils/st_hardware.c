// ============================================================================
//  st_hardware.c —— STM32 平台杂项硬件辅助
// ----------------------------------------------------------------------------
//  - GetSerialNumber: 读取芯片唯一 ID（96 bit）压缩为 64 bit 序列号
//  - AdcGetChipTemperature: 读内部温度传感器（Tim1Callback100Hz 调用）
// ============================================================================
#include "st_hardware.h"
#include "stm32f103xb.h"

uint64_t GetSerialNumber()
{
    // 下面生成 USB 序列号的过程与 ST 内置 USB Bootloader 使用的方式一致。
    // 这样设备在正常模式与 DFU 模式下拥有相同的序列号。
    uint32_t uuid0 = *(uint32_t * )(UID_BASE + 0);
    uint32_t uuid1 = *(uint32_t * )(UID_BASE + 4);
    uint32_t uuid2 = *(uint32_t * )(UID_BASE + 8);
    uint32_t uuid_mixed_part = uuid0 + uuid2;
    uint64_t serialNumber = ((uint64_t) uuid_mixed_part << 16) | (uint64_t)(uuid1 >> 16);

    return serialNumber;
}