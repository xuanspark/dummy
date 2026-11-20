#ifndef REF_FIXED_FORMAT_HPP
#define REF_FIXED_FORMAT_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// 使用整数格式化输出定点小数，避免 newlib 浮点 printf 申请临时堆内存。
inline bool FormatFixed(char* output, std::size_t capacity, float value, unsigned decimals)
{
    if (output == nullptr || capacity == 0) return false;
    if (decimals > 6) decimals = 6;
    if (!std::isfinite(value)) {
        const char* text = std::isnan(value) ? "nan" : (value < 0.0f ? "-inf" : "inf");
        std::strncpy(output, text, capacity - 1);
        output[capacity - 1] = '\0';
        return false;
    }

    static constexpr uint64_t scales[] = {1ULL, 10ULL, 100ULL, 1000ULL,
                                          10000ULL, 100000ULL, 1000000ULL};
    const uint64_t scale = scales[decimals];
    const bool negative = std::signbit(value);
    const double magnitude = std::fabs(static_cast<double>(value));
    const uint64_t scaled = static_cast<uint64_t>(magnitude * static_cast<double>(scale) + 0.5);
    const uint64_t whole = scaled / scale;
    const uint64_t fraction = scaled % scale;

    int written = 0;
    if (decimals == 0) {
        written = std::snprintf(output, capacity, "%s%llu",
                                negative ? "-" : "",
                                static_cast<unsigned long long>(whole));
    } else {
        written = std::snprintf(output, capacity, "%s%llu.%0*llu",
                                negative ? "-" : "",
                                static_cast<unsigned long long>(whole),
                                static_cast<int>(decimals),
                                static_cast<unsigned long long>(fraction));
    }
    if (written < 0 || static_cast<std::size_t>(written) >= capacity) {
        output[capacity - 1] = '\0';
        return false;
    }
    return true;
}

#endif
