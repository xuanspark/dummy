// ============================================================================
//  emulated_eeprom.h —— STM32 Flash 模拟 EEPROM 的 C 接口声明（42 子板）
// ----------------------------------------------------------------------------
//  - 维护 1 KB 内存缓冲，启动后从 Flash 末页加载，commit 时写回
//  - F1xx 系列按页擦写，commit ~50 ms
// ============================================================================
/* ----- 包含守卫，避免重复包含 ----------------------------------- */
#ifndef __STM32_EEPROM_H
#define __STM32_EEPROM_H

/* Includes ------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C" {
#include <stm32f103xb.h>
#include <stm32f1xx_hal.h>

#endif

/* ----- 导出类型 / 导出常量 -------------------------------------- */
#if defined(STM32MP1xx)
/* STM32MP1xx 设备说明：
 *   该系列没有非易失存储器，模拟只能在 RETRAM 中完成。
 *   仅在 VBAT 供电（例如 STM32MP157A_DK1 的 CN3 接上纽扣电池）
 *   且协处理器从 STANDBY 模式唤醒时才能保留数据。
 *   冷启动不会保留，即使接有 VBAT。
 * See: https://community.st.com/s/question/0D50X0000B44pHUSQY/doesnt-the-mcu-coprocessor-have-nonvolatile-memory
 */
#define EEPROM_RETRAM_MODE
/* 4 KB 大小与 ATMega2560 的 EEPROM 一致。 */
#ifndef EEPROM_RETRAM_MODE_SIZE
#define EEPROM_RETRAM_MODE_SIZE ((uint32_t)(4*1024))
#endif
/* RETRAM 起始地址为 0x00000000（复位入口）、结束地址为 0x00020000（64 KB 全部）。
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
#define FLASH_PAGE_SIZE     ((uint32_t)(1*1024)) /* 1kB page */
#endif

#if defined(DATA_EEPROM_BASE) || defined(FLASH_EEPROM_BASE)

#if defined (DATA_EEPROM_END)
#define E2END (DATA_EEPROM_END - DATA_EEPROM_BASE)
#elif defined (DATA_EEPROM_BANK2_END)
/* 假设两个连续的 bank */
#define DATA_EEPROM_END DATA_EEPROM_BANK2_END
#define E2END (DATA_EEPROM_BANK2_END - DATA_EEPROM_BASE)
#elif defined (FLASH_EEPROM_END)
#define DATA_EEPROM_BASE FLASH_EEPROM_BASE
#define DATA_EEPROM_END FLASH_EEPROM_END
#define E2END (DATA_EEPROM_END - DATA_EEPROM_BASE)
#endif /* __EEPROM_END */

#else /* _EEPROM_BASE */
#define E2END (FLASH_PAGE_SIZE - 1)
#endif /* _EEPROM_BASE */

#endif


/* ----- 导出宏 / 导出函数 ------------------------------------- */

uint8_t eeprom_read_byte(uint32_t pos);
void eeprom_write_byte(uint32_t pos, uint8_t value);

#if !defined(DATA_EEPROM_BASE)
void eeprom_buffer_fill();
void eeprom_buffer_flush();
uint8_t eeprom_buffered_read_byte(uint32_t pos);
void eeprom_buffered_write_byte(uint32_t pos, uint8_t value);
#endif /* ! DATA_EEPROM_BASE */

#ifdef __cplusplus
}


#endif
#endif /* __STM32_EEPROM_H */

/************************ (C) COPYRIGHT STMicroelectronics — 文件结束 ****/
