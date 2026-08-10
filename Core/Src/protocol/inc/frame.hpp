#pragma once

#include <cstdint>
#include <vector>
#include <array>

namespace protocol {

constexpr size_t PREAMBLE_LEN = 2; 
constexpr std::array<uint8_t, PREAMBLE_LEN> PREAMBLE_BYTES = {0xAA, 0x55};
constexpr size_t PAYLOAD_LEN_SIZE = 2; 

constexpr uint8_t CURRENT_VERSION = 0x01;

enum class MessageType : uint8_t {
    DATA        = 0x01,
    ACK         = 0x02,
    NACK        = 0x03,
    GET_STATS   = 0x10,
    STATS_RESP  = 0x11,
    UNKNOWN     = 0xFF
};

enum class ParseError : uint8_t {
    NONE = 0,
    INVALID_CRC,          // Ошибка контрольной суммы
    PAYLOAD_TOO_LARGE,    // Длина превышает лимит памяти
    FRAME_INCOMPLETE,     // Обрыв кадра (таймаут между байтами)
    UNEXPECTED_BYTE,      // Потеря синхронизации / байт вне протокола
    BUFFER_OVERFLOW       // Переполнение внутреннего буфера
};

#pragma pack(push, 1)
struct FrameHeader {
    uint8_t     version{CURRENT_VERSION};
    MessageType type{MessageType::UNKNOWN};
    uint8_t     seq_num{0};          // <--- ОБЯЗАТЕЛЬНО: Номер последовательности для ACK/NACK
    uint16_t    payload_len{0};      // Длина полезной нагрузки

    static constexpr size_t size() {
        return sizeof(version) + sizeof(type) + sizeof(seq_num) + sizeof(payload_len);
    }
};
#pragma pack(pop)

struct Frame : public FrameHeader {
    std::vector<uint8_t> payload;
    uint16_t             crc16{0};

    bool is_version_valid() const {
        return version == CURRENT_VERSION;
    }
};

} // namespace protocol