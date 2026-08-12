#include "FSM_parser.hpp"
#include "crc16.hpp"

namespace protocol {
constexpr size_t MAX_ALLOWED_PAYLOAD_SIZE = 512;

FrameParser::FrameParser(DiagnosticsStats &stats, FrameCallback on_frame_cb)
    : stats_(stats), on_frame_cb_(on_frame_cb) {
  reset();
}

void FrameParser::reset() {
  state_ = ParserState::WAIT_SYNC;
  bytes_read_ = 0;
  sync_index_ = 0;
  rx_crc_ = 0;
  rx_frame_ = Frame{};
}

void FrameParser::transition_to(ParserState new_state) {
  state_ = new_state;
  bytes_read_ = 0;
}

void FrameParser::process_buffer(const uint8_t *buffer, size_t size) {
  if (!buffer)
    return;
  for (size_t i = 0; i < size; ++i) {
    (void)process_byte(buffer[i]);
  }
}

std::expected<void, ParseError> FrameParser::process_byte(uint8_t byte) {
  switch (state_) {

  // ====================================================================
  // 1. WAIT_SYNC: Поиск преамбулы
  // ====================================================================
  case ParserState::WAIT_SYNC: {
    if (byte == PREAMBLE_BYTES[sync_index_]) {
      sync_index_++;
      if (sync_index_ == PREAMBLE_LEN) {
        transition_to(ParserState::HEADER);
      }
    } else {
      sync_index_ = (byte == PREAMBLE_BYTES[0]) ? 1 : 0;
    }
    break;
  }

  // ====================================================================
  // 2. HEADER: Чтение версии, типа, seq_num и длины payload
  // ====================================================================
  case ParserState::HEADER: {
    if (bytes_read_ == 0) {
      rx_frame_.version = byte;
    } else if (bytes_read_ == 1) {
      rx_frame_.type = static_cast<MessageType>(byte);
    } else if (bytes_read_ == 2) {
      rx_frame_.seq_num = byte; // <--- Считываем seq_num
    }
#if PAYLOAD_LEN_SIZE == 2
    else if (bytes_read_ == 3) {
      rx_frame_.payload_len = static_cast<uint16_t>(byte << 8); // High Byte
    } else if (bytes_read_ == 4) {
      rx_frame_.payload_len |= byte; // Low Byte
    }
#else
    else if (bytes_read_ == 3) {
      rx_frame_.payload_len = byte;
    }
#endif

    bytes_read_++;

    if (bytes_read_ == FrameHeader::size()) {
      // Защита от переполнения памяти МК
      if (rx_frame_.payload_len > MAX_ALLOWED_PAYLOAD_SIZE) {
        stats_.length_errors++;
        stats_.resync_events++;
        reset();

        if (on_frame_cb_) {
          on_frame_cb_(std::unexpected(ParseError::PAYLOAD_TOO_LARGE));
        }
        return std::unexpected(ParseError::PAYLOAD_TOO_LARGE);
      }

      if (rx_frame_.payload_len > 0) {
        rx_frame_.payload.resize(rx_frame_.payload_len);
        transition_to(ParserState::PAYLOAD);
      } else {
        transition_to(ParserState::CRC);
      }
    }
    break;
  }

  // ====================================================================
  // 3. PAYLOAD: Чтение полезных данных
  // ====================================================================
  case ParserState::PAYLOAD: {
    rx_frame_.payload[bytes_read_] = byte;
    bytes_read_++;

    if (bytes_read_ == rx_frame_.payload_len) {
      transition_to(ParserState::CRC);
    }
    break;
  }

  // ====================================================================
  // 4. CRC: Вычитывание и валидация CRC16
  // ====================================================================
  case ParserState::CRC: {
    if (bytes_read_ == 0) {
      rx_crc_ = static_cast<uint16_t>(byte << 8);
      bytes_read_++;
    } else if (bytes_read_ == 1) {
      rx_crc_ |= byte;

      if (validate_crc()) {
        rx_frame_.crc16 = rx_crc_;
        stats_.rx_frames_ok++;

        transition_to(ParserState::DISPATCH);
        return process_byte(0); // Запуск DISPATCH
      } else {
        stats_.crc_errors++;
        stats_.resync_events++;
        reset();

        if (on_frame_cb_) {
          on_frame_cb_(std::unexpected(ParseError::INVALID_CRC));
        }
        return std::unexpected(ParseError::INVALID_CRC);
      }
    }
    break;
  }

  // ====================================================================
  // 5. DISPATCH: Передача кадра в приложение
  // ====================================================================
  case ParserState::DISPATCH: {
    if (on_frame_cb_) {
      on_frame_cb_(rx_frame_); // Передача валидного Frame
    }
    reset();
    break;
  }
  }

  return {}; // Возврат успешного std::expected<void, ParseError>
}

bool FrameParser::validate_crc() {
  // 1. Создаем временный буфер под заголовок (5 байт) + payload
  std::vector<uint8_t> crc_buffer;
  crc_buffer.reserve(5 + rx_frame_.payload.size());

  // 2. Заполняем заголовок в Big-Endian формате (соответствует
  // struct.pack(">BBBH"))
  crc_buffer.push_back(rx_frame_.version);
  crc_buffer.push_back(static_cast<uint8_t>(rx_frame_.type));
  crc_buffer.push_back(rx_frame_.seq_num);
  crc_buffer.push_back(
      static_cast<uint8_t>((rx_frame_.payload_len >> 8) & 0xFF)); // MSB
  crc_buffer.push_back(
      static_cast<uint8_t>(rx_frame_.payload_len & 0xFF)); // LSB

  // 3. Добавляем полезную нагрузку
  if (!rx_frame_.payload.empty()) {
    crc_buffer.insert(crc_buffer.end(), rx_frame_.payload.begin(),
                      rx_frame_.payload.end());
  }

  // 4. Считаем CRC16 в один проход
  uint16_t calculated_crc =
      Crc16::calculate(crc_buffer.data(), crc_buffer.size());

  // 5. Сравниваем с принятым rx_crc_ (собранным как (byte_high << 8) |
  // byte_low)
  return calculated_crc == rx_crc_;
}
} // namespace protocol
