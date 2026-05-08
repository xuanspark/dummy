// ============================================================================
//  eeprom_interface.h —— STM32 Flash 模拟 EEPROM 的 Arduino 风格薄壳（42 子板）
// ----------------------------------------------------------------------------
//  - 与 dummy-ref-core-fw 同名文件功能基本相同，但类型名为 EEPROM（而非 EEPROMClass）
//  - 实际 Flash 抹写由 emulated_eeprom 提供
//  - 上层在 main.cpp 中通过 EEPROM eeprom; eeprom.get(0, boardConfig) 读写
// ============================================================================
#ifndef FlashStorage_STM32_h
#define FlashStorage_STM32_h


#if !(defined(STM32F0) || defined(STM32F1) || defined(STM32F2) || defined(STM32F3) || defined(STM32F4) || defined(STM32F7) || \
       defined(STM32L0) || defined(STM32L1) || defined(STM32L4) || defined(STM32H7) || defined(STM32G0) || defined(STM32G4) || \
       defined(STM32WB) || defined(STM32MP1) || defined(STM32L5))
#error This code is intended to run on STM32F/L/H/G/WB/MP1 platform! Please check your Tools->Board setting.
#endif

#define FLASH_STORAGE_STM32_VERSION     "FlashStorage_STM32 v1.1.0"

// 仅用于“无内置 EEPROM、需 Flash 模拟”的芯片（F1xx 在本项目使用）。
#if !defined(DATA_EEPROM_BASE)

#include "emulated_eeprom.h"

class EEPROM
{
public:

    EEPROM() : _initialized(false), _dirtyBuffer(false), _commitASAP(true), _validEEPROM(true)
    {}

    /**
     * 读一个 EEPROM 单元
     * @param address 字节偏移
     * @return 该偏移上的字节值
     */
    uint8_t read(int address)
    {
        if (!_initialized)
            init();

        return eeprom_buffered_read_byte(address);
    }

    /**
     * 更新一个 EEPROM 单元（仅在值变化时标记 dirty）
     */
    void update(int address, uint8_t value)
    {
        if (!_initialized)
            init();

        if (eeprom_buffered_read_byte(address) != value)
        {
            _dirtyBuffer = true;
            eeprom_buffered_write_byte(address, value);
        }
    }

    /**
     * 写入一个 EEPROM 单元（update 的别名，Arduino 生态习惯写法）
     */
    void write(int address, uint8_t value)
    {
        update(address, value);
    }

    /**
     * 以字节拷贝从 EEPROM 读取一个对象
     * @param idx 起始字节偏移
     * @param t   接收目标，调用后被填足
     */
    // get / put 接口：在 EEPROM 与任意类型对象之间传递字节。
    template<typename T>
    T &get(int _offset, T &_t)
    {
        // 若尚未从 Flash 加载到内存缓冲区，先初始化
        if (!_initialized)
            init();

        uint16_t offset = _offset;
        uint8_t* _pointer = (uint8_t*) &_t;

        for (uint16_t count = sizeof(T); count; --count, ++offset)
        {
            *_pointer++ = eeprom_buffered_read_byte(offset);
        }

        return _t;
    }

    template<typename T>
    const T &put(int idx, const T &t)
    {
        // 若尚未从 Flash 加载到内存缓冲区，先初始化
        if (!_initialized)
            init();

        uint16_t offset = idx;

        const uint8_t* _pointer = (const uint8_t*) &t;

        for (uint16_t count = sizeof(T); count; --count, ++offset)
        {
            eeprom_buffered_write_byte(offset, *_pointer++);
        }

        if (_commitASAP)
        {
            // 立即把缓冲区刷入 Flash（默认行为）
            eeprom_buffer_flush();

            _dirtyBuffer = false;
            _validEEPROM = true;
        } else
        {
            // 延迟提交：仅记 dirty，等调用方后续调 commit()
            _dirtyBuffer = true;
        }

        return t;
    }

    /**
     * 判断 EEPROM 中是否存有有效数据（至少写入过一次）
     */
    bool isValid()
    {
        return _validEEPROM;
    }

    /**
     * 将迭代修改一次刷入 Flash。谨慎调用：
     * 每次 commit 都会损耗 Flash 寿命（与所有 Flash 存储器一样）。
     */
    void commit()
    {
        if (!_initialized)
            init();

        if (_dirtyBuffer)
        {
            // 把缓冲区写回 Flash（耗时操作）
            eeprom_buffer_flush();

            _dirtyBuffer = false;
            _validEEPROM = true;
        }
    }

    uint16_t length()
    { return E2END + 1; }

    void setCommitASAP(bool value = true)
    { _commitASAP = value; }
    bool getCommitASAP()
    { return _commitASAP; }

private:

    void init()
    {
        // 从 Flash 拷一个页到内存缓冲区（仅启动后第一次调用）
        eeprom_buffer_fill();
        _initialized = true;
    }

    bool _initialized;
    bool _dirtyBuffer;
    bool _commitASAP;
    bool _validEEPROM;
};


#else

#include "EEPROM.h"

#endif    // #if !defined(DATA_EEPROM_BASE)

#endif    //#ifndef FlashAsEEPROM_SAMD_h
