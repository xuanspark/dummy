#pragma once

#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace MotionParser {

// 逐项读取，不创建整行副本，也不进入浮点 scanf 的大栈帧。
inline bool ReadNumber(const char*& cursor, float& value)
{
    char* end = nullptr;
    errno = 0;
    value = std::strtof(cursor, &end);
    if (end == cursor || errno == ERANGE || !std::isfinite(value)) return false;
    cursor = end;
    return true;
}

// 入口校验、队列执行和模式 2 替换共用同一语法；省略速度时保留调用方默认值。
inline bool Parse(const char* command, float (&joints)[6], float& speed)
{
    if (!command || (command[0] != '&' && command[0] != '@' && command[0] != '>')) return false;
    const char* cursor = command + 1;
    for (int index = 0; index < 6; ++index) {
        if (!ReadNumber(cursor, joints[index])) return false;
        if (index < 5) {
            if (*cursor != ',') return false;
            ++cursor;
        }
    }
    if (*cursor == ',') {
        ++cursor;
        if (!ReadNumber(cursor, speed)) return false;
    }
    return *cursor == '\0' && std::isfinite(speed) && speed > 0 && speed <= 100;
}

inline bool ParsePercent(const char* text, float& percent)
{
    if (!text || !ReadNumber(text, percent)) return false;
    return *text == '\0' && percent >= 0 && percent <= 100;
}

}
