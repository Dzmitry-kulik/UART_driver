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
  uint16_t calculated_crc = 0xFFFF;

  // Формируем байты полного заголовка (version + type + seq_num + payload_len)
  uint8_t header_bytes[FrameHeader::size()];
  header_bytes[0] = rx_frame_.version;
  header_bytes[1] = static_cast<uint8_t>(rx_frame_.type);
  header_bytes[2] = rx_frame_.seq_num;

#if PAYLOAD_LEN_SIZE == 2
  header_bytes[3] = static_cast<uint8_t>((rx_frame_.payload_len >> 8) & 0xFF);
  header_bytes[4] = static_cast<uint8_t>(rx_frame_.payload_len & 0xFF);
#else
  header_bytes[3] = static_cast<uint8_t>(rx_frame_.payload_len & 0xFF);
#endif
  calculated_crc =
      Crc16::calculate(header_bytes, sizeof(header_bytes), calculated_crc);
  if (!rx_frame_.payload.empty()) {
    calculated_crc = Crc16::calculate(rx_frame_.payload.data(),
                                      rx_frame_.payload.size(), calculated_crc);
  }

  return calculated_crc == rx_crc_;
}
} // namespace protocol
