// ============================================================================
//  eeprom_interface.h —— Flash 模拟 EEPROM 的 Arduino 风格薄壳
// ----------------------------------------------------------------------------
//  - EEPROMClass 提供 read/write/get/put 等接口，行为模拟 Arduino EEPROM 库
//  - 底层通过 emulated_eeprom 把 1KB 数据缓存到 Flash 一个扇区
//  - 写操作均要先 read → 修改 → commit，开销大，避免高频写
// ============================================================================
#ifndef FlashStorage_STM32_h
#define FlashStorage_STM32_h


#if !(defined(STM32F0) || defined(STM32F1) || defined(STM32F2) || defined(STM32F3) || defined(STM32F4) || defined(STM32F7) || \
       defined(STM32L0) || defined(STM32L1) || defined(STM32L4) || defined(STM32H7) || defined(STM32G0) || defined(STM32G4) || \
       defined(STM32WB) || defined(STM32MP1) || defined(STM32L5))
#error This code is intended to run on STM32F/L/H/G/WB/MP1 platform! Please check your Tools->Board setting.
#endif

#define FLASH_STORAGE_STM32_VERSION     "FlashStorage_STM32 v1.1.0"

// 仅用于无内置 EEPROM、需 Flash 模拟的芯片（F2/F4/F7 等）。
// 含集成 EEPROM 的芯片（如 L1）由下方 #else 分支引入 Arduino 原生 EEPROM.h。
#if !defined(DATA_EEPROM_BASE)

#include "emulated_eeprom.h"

class EEPROMClass
{
public:

    // 默认 _commitASAP=true、_validEEPROM=true 表示 put() 后立即刷入 Flash、
    // 初始认为有效数据。实际首次 read/write 会触发 init() 从 Flash 填缓冲区。
    EEPROMClass() : _initialized(false), _dirtyBuffer(false), _commitASAP(true), _validEEPROM(true)
    {}

    // 读一个字节。首次调用会自动从 Flash 加载进缓冲区。
    uint8_t read(int address)
    {
        if (!_initialized)
            init();

        return eeprom_buffered_read_byte(address);
    }

    // 读-改-写：只有值变化才标记 dirty，避免无谓的 Flash 耗损。
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

    // write() 是 update() 的别名，Arduino 生态习惯写法。实质制定同 update。
    void write(int address, uint8_t value)
    {
        update(address, value);
    }

    // get<T>: 从 EEPROM 读一个任意类型的对象（按字节拷贝）。
    //   idx 为起始字节偏移，t 为收取目标，调用后 t 被填足。
    //   对于不是 trivially-copyable 的类型谈不到习惯，调用方自负。
    template<typename T>
    T &get(int idx, T &t)
    {
        // 若尚未从 Flash 加载到内存缓冲区，先初始化
        if (!_initialized)
            init();

        uint16_t offset = idx;
        uint8_t* _pointer = (uint8_t * ) & t;

        for (uint16_t count = sizeof(T); count; --count, ++offset)
        {
            *_pointer++ = eeprom_buffered_read_byte(offset);
        }

        return t;
    }

    // put<T>: 将一个对象写入 EEPROM。若 _commitASAP 为 true，写后立即
    // flush 到 Flash；否则仅记录 dirty，由调用方后续调 commit() 统一写入。
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
            // 延迟提交：仅记 dirty，等调用方后续显式调 commit() 一并写入
            _dirtyBuffer = true;
        }

        return t;
    }

    // 判断 EEPROM 中是否存有有效数据（至少写入过一次）。
    bool isValid()
    {
        return _validEEPROM;
    }

    // 将迭代修改一次刷入 Flash。谨慎调用：每次 commit 都会损害 Flash 寿命。
    // 需高频修改时建议定 setCommitASAP(false) 后集中调 commit。
    void commit()
    {
        if (!_initialized)
            init();

        if (_dirtyBuffer)
        {
            // 把缓冲区写回 Flash（耗时操作，~100 ms 量级）
            eeprom_buffer_flush();

            _dirtyBuffer = false;
            _validEEPROM = true;
        }
    }

    // EEPROM 容量（字节数）。由 emulated_eeprom.h 里的 E2END 决定。
    uint16_t length()
    { return E2END + 1; }

    // 控制 put() 是否立即刷入 Flash。false=延迟提交，用于批量写。
    void setCommitASAP(bool value = true)
    { _commitASAP = value; }
    bool getCommitASAP()
    { return _commitASAP; }

private:

    // 首次 read/write 前被调用：从 Flash 拷 1 个扇区进内存缓冲区。
    void init()
    {
        // 从 Flash 拷一个扇区到内存缓冲区（仅启动后第一次调用）
        eeprom_buffer_fill();
        _initialized = true;
    }

    bool _initialized;     // 内存缓冲区是否已从 Flash 加载
    bool _dirtyBuffer;     // 是否有未刷入 Flash 的修改
    bool _commitASAP;      // put 后立即 commit？
    bool _validEEPROM;     // 当前 EEPROM 内容是否被视为有效
};


#else

// 含集成 EEPROM 的芯片：直接走 Arduino 原生 EEPROM 库
#include "EEPROM.h"

#endif    // !defined(DATA_EEPROM_BASE) —— 集成 vs 模拟 二选一结束

#endif    // FlashStorage_STM32_h —— 头文件包含守卫结束
