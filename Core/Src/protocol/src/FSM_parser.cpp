#include "FSM_parser.hpp"
#include "crc16.hpp"
#include "stm32f4xx_hal.h"

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

void FrameParser::check_timeout(uint32_t current_time_ms, uint32_t timeout_ms) {
  // Если парсер находится в процессе сборки кадра, но новые байты долго не
  // поступают
  if (state_ != ParserState::WAIT_SYNC &&
      (current_time_ms - last_byte_time_ms_ > timeout_ms)) {
    stats_.timeout_errors++;
    stats_.resync_events++;
    reset(); // Сбрасываем FSM в состояние WAIT_SYNC по таймауту обрыва кадра
  }
}

std::expected<void, ParseError> FrameParser::process_byte(uint8_t byte) {
  // Фиксируем системное время прихода последнего байта
  last_byte_time_ms_ = HAL_GetTick();

  switch (state_) {

  // ====================================================================
  // 1. WAIT_SYNC: Поиск преамбулы (0xAA 0x55)
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
  // 2. HEADER: Чтение версии, типа, seq_num и 2-байтовой длины payload
  // ====================================================================
  case ParserState::HEADER: {
    if (bytes_read_ == 0) {
      rx_frame_.version = byte;
    } else if (bytes_read_ == 1) {
      rx_frame_.type = static_cast<MessageType>(byte);
    } else if (bytes_read_ == 2) {
      rx_frame_.seq_num = byte;
    } else if (bytes_read_ == 3) {
      rx_frame_.payload_len = static_cast<uint16_t>(byte << 8); // MSB
    } else if (bytes_read_ == 4) {
      rx_frame_.payload_len |= byte; // LSB
    }

    bytes_read_++;

    if (bytes_read_ == 5) {
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
  // 3. PAYLOAD: Чтение данных
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
  // 4. CRC: Вычитывание 2-байтовой суммы (Big-Endian) и валидация
  // ====================================================================
  case ParserState::CRC: {
    if (bytes_read_ == 0) {
      rx_crc_ = static_cast<uint16_t>(byte << 8); // MSB
      bytes_read_++;
    } else if (bytes_read_ == 1) {
      rx_crc_ |= byte; // LSB

      if (validate_crc()) {
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
      on_frame_cb_(rx_frame_);
    }
    reset();
    break;
  }
  }

  return {};
}

bool FrameParser::validate_crc() {
  std::vector<uint8_t> crc_buffer;
  crc_buffer.reserve(5 + rx_frame_.payload.size());

  crc_buffer.push_back(rx_frame_.version);
  crc_buffer.push_back(static_cast<uint8_t>(rx_frame_.type));
  crc_buffer.push_back(rx_frame_.seq_num);
  crc_buffer.push_back(
      static_cast<uint8_t>((rx_frame_.payload_len >> 8) & 0xFF));
  crc_buffer.push_back(static_cast<uint8_t>(rx_frame_.payload_len & 0xFF));

  if (!rx_frame_.payload.empty()) {
    crc_buffer.insert(crc_buffer.end(), rx_frame_.payload.begin(),
                      rx_frame_.payload.end());
  }

  uint16_t calculated_crc =
      Crc16::calculate(crc_buffer.data(), crc_buffer.size());

  return calculated_crc == rx_crc_;
}

} // namespace protocol
