#pragma once
#include <cstdint>
class EEPROMClass {
public:
    uint8_t read(int) { return 11; }
    void write(int, uint8_t) {}
};
