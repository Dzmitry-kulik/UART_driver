#include "FSM_parser.hpp"
#include "crc16.hpp"

namespace protocol {

// Максимальный размер payload, совпадающий с Python (512 байт)
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
      // Читаем длину в формате Big-Endian (MSB)
      rx_frame_.payload_len = static_cast<uint16_t>(byte << 8);
    } else if (bytes_read_ == 4) {
      // Читаем длину в формате Big-Endian (LSB)
      rx_frame_.payload_len |= byte;
    }

    bytes_read_++;

    // Заголовок в нашем протоколе всегда равен 5 байтам
    if (bytes_read_ == 5) {
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
  // 5. DISPATCH: Передача валидного кадра в приложение (main.cpp)
  // ====================================================================
  case ParserState::DISPATCH: {
    if (on_frame_cb_) {
      on_frame_cb_(rx_frame_);
    }
    reset();
    break;
  }
  }

  return {}; // Успешный разбор текущего байта
}

bool FrameParser::validate_crc() {
  // 1. Создаем буфер для расчета (5 байт заголовка + payload)
  std::vector<uint8_t> crc_buffer;
  crc_buffer.reserve(5 + rx_frame_.payload.size());

  // 2. Формируем заголовок в Big-Endian (строго совпадает с Python
  // struct.pack(">BBBH"))
  crc_buffer.push_back(rx_frame_.version);
  crc_buffer.push_back(static_cast<uint8_t>(rx_frame_.type));
  crc_buffer.push_back(rx_frame_.seq_num);
  crc_buffer.push_back(
      static_cast<uint8_t>((rx_frame_.payload_len >> 8) & 0xFF)); // Длина MSB
  crc_buffer.push_back(
      static_cast<uint8_t>(rx_frame_.payload_len & 0xFF)); // Длина LSB

  // 3. Добавляем полезную нагрузку
  if (!rx_frame_.payload.empty()) {
    crc_buffer.insert(crc_buffer.end(), rx_frame_.payload.begin(),
                      rx_frame_.payload.end());
  }

  // 4. Считаем CRC16 в один проход
  uint16_t calculated_crc =
      Crc16::calculate(crc_buffer.data(), crc_buffer.size());

  // 5. Сравниваем с принятым rx_crc_
  return calculated_crc == rx_crc_;
}

} // namespace protocol
