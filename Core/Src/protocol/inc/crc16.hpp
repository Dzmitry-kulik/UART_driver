#pragma once

#include <cstdint>
#include <cstddef>

namespace protocol {
class Crc16 {
public:
    static uint16_t calculate(const uint8_t* data, size_t length, uint16_t init_crc = 0xFFFF) {
        uint16_t crc = init_crc;
        for (size_t i = 0; i < length; ++i) {
            crc ^= (static_cast<uint16_t>(data[i]) << 8);
            for (uint8_t bit = 0; bit < 8; ++bit) {
                if (crc & 0x8000) {
                    crc = (crc << 1) ^ 0x1021;
                } else {
                    crc = (crc << 1);
                }
            }
        }
        return crc;
    }
};
} 