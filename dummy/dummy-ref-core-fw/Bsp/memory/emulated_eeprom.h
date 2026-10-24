// ============================================================================
//  emulated_eeprom.h —— STM32 Flash 模拟 EEPROM 的 C 接口声明
// ----------------------------------------------------------------------------
//  - 在 SRAM 维护 EEPROM_SIZE 字节缓冲，启动时从 Flash 加载、commit 时写回
//  - 提供 byte / word / dword / float 多种粒度读写
//  - F4 系列要求按扇区擦写，因此 commit 是 ~100 ms 级别的操作
// ============================================================================
/* ----- 包含守卫，避免重复包含 ----------------------------------- */
#ifndef __STM32_EEPROM_H
#define __STM32_EEPROM_H

/* ----- 包含头文件 ------------------------------------------------ */

#ifdef __cplusplus
extern "C" {
#include "main.h"
#endif

/* ----- 导出类型 / 导出常量 -------------------------------------- */
#if defined(STM32MP1xx)
/* STM32MP1xx 设备说明：
 *   该系列没有非易失存储器，模拟只能在 RETRAM 中完成。
 *   仅在 VBAT 供电（例如 STM32MP157A_DK1 的 CN3 接上纽扣电池）
 *   且协处理器从 STANDBY 模式唤醒时才能保留数据。
 *   冷启动不会保留，即使接有 VBAT。
 *   参考：https://community.st.com/s/question/0D50X0000B44pHUSQY
 */
#define EEPROM_RETRAM_MODE
/* 4kB 大小与 ATMega2560 的 EEPROM 一致。 */
#ifndef EEPROM_RETRAM_MODE_SIZE
#define EEPROM_RETRAM_MODE_SIZE ((uint32_t)(4*1024))
#endif
/* RETRAM 起始地址为 0x00000000（复位入口）、结束地址为 0x00020000 （64 KB 全部）。
 * STM32MP1xx 默认 ldscript.ld 未定义 0x00000298（ISR 向量末端）到 0x00020000 之间的区间，
 * 所以在该区间使用是安全的。请确保 ldscript.ld 不与以下地址区间重叠。
 */
#ifndef EEPROM_RETRAM_START_ADDRESS
#define EEPROM_RETRAM_START_ADDRESS (0x00000400UL)
#endif
#define E2END (EEPROM_RETRAM_MODE_SIZE - 1)
#else
#ifndef FLASH_PAGE_SIZE
/*
 * STM32F2xx / STM32F4xx / STM32F7xx 未定义 FLASH_PAGE_SIZE。
 * 可在 variant.h 或 build_opt.h 中重定义。
 * 注意：这不是扇区大小，仅是 EEPROM 模拟使用的大小。
 * 但擦除时会抹除整个扇区，请勿将该扇区用于其他用途。
 */
#define FLASH_PAGE_SIZE     ((uint32_t)(1*1024)) /* 1 KB 页 */
#endif

#if defined(DATA_EEPROM_BASE) || defined(FLASH_EEPROM_BASE)

#if defined (DATA_EEPROM_END)
#define E2END (DATA_EEPROM_END - DATA_EEPROM_BASE)
#elif defined (DATA_EEPROM_BANK2_END)
/* 假设两个起始堆在一起的 bank */
#define DATA_EEPROM_END DATA_EEPROM_BANK2_END
#define E2END (DATA_EEPROM_BANK2_END - DATA_EEPROM_BASE)
#elif defined (FLASH_EEPROM_END)
#define DATA_EEPROM_BASE FLASH_EEPROM_BASE
#define DATA_EEPROM_END FLASH_EEPROM_END
#define E2END (DATA_EEPROM_END - DATA_EEPROM_BASE)
#endif /* __EEPROM_END —— 决定集成 EEPROM 大小 */

#else /* _EEPROM_BASE —— 无集成 EEPROM，走 Flash 页模拟路径 */
#define E2END (FLASH_PAGE_SIZE - 1)
#endif /* _EEPROM_BASE */

#endif


/* ----- 对外导出函数（供 eeprom_interface.h 调用）----------------------- */

// 直读 / 直写一个字节（跳过内存缓冲区，仅在集成 EEPROM 的芯片上可用）。
uint8_t eeprom_read_byte(uint32_t pos);
void eeprom_write_byte(uint32_t pos, uint8_t value);

#if !defined(DATA_EEPROM_BASE)
// 以下函数仅在“无集成 EEPROM、需 Flash 模拟”的芯片上存在：
void eeprom_buffer_fill();                                       // Flash → 内存缓冲区
void eeprom_buffer_flush();                                      // 内存缓冲区 → Flash（抹除扬屣谌扬区）
uint8_t eeprom_buffered_read_byte(uint32_t pos);                 // 读内存缓冲区
void eeprom_buffered_write_byte(uint32_t pos, uint8_t value);    // 写内存缓冲区（未刷入 Flash）
#endif /* ! DATA_EEPROM_BASE */

#ifdef __cplusplus
}


#endif // __cplusplus
#endif /* __STM32_EEPROM_H —— 头文件包含守卫结束 */

/************************ (C) COPYRIGHT STMicroelectronics — 文件结束 ****/
