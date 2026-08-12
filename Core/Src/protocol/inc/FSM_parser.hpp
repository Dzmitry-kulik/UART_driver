#pragma once

#include "diagnostics.hpp"
#include "frame.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>

namespace protocol {

/**
 * @brief Ошибки, возникающие в процессе парсинга кадра
 */
enum class ParseError : uint8_t {
  NONE = 0,
  INVALID_CRC,
  PAYLOAD_TOO_LARGE,
  FRAME_INCOMPLETE,
  UNEXPECTED_BYTE
};

/**
 * @brief Состояния конечного автомата (FSM) парсера
 */
enum class ParserState : uint8_t { WAIT_SYNC, HEADER, PAYLOAD, CRC, DISPATCH };

/// Колбэк для передачи обработанного кадра или ошибки
using FrameCallback = std::function<void(std::expected<Frame, ParseError>)>;

/**
 * @brief Побайтовый FSM-парсер протокола передачи данных
 */
class FrameParser {
public:
  explicit FrameParser(DiagnosticsStats &stats,
                       FrameCallback on_frame_cb = nullptr) noexcept;

  /**
   * @brief Обработка одиночного байта из входного потока
   * @param byte Принятый байт
   * @return std::expected<void, ParseError> статус обработки
   */
  std::expected<void, ParseError> process_byte(uint8_t byte);

  /**
   * @brief Последовательная обработка буфера байт
   * @param buffer Указатель на массив данных
   * @param size Длина массива
   */
  void process_buffer(const uint8_t *buffer, size_t size);

  /**
   * @brief Сброс состояния автомата в исходное (WAIT_SYNC)
   */
  void reset() noexcept;

  /**
   * @brief Установка нового колбэка
   */
  void set_callback(FrameCallback cb) noexcept { on_frame_cb_ = cb; }

  /**
   * @brief Проверка межбайтового таймаута
   * @param current_time_ms Текущее системное время в миллисекундах
   * @param timeout_ms Допустимый интервал между байтами (по умолчанию 50 мс)
   */
  void check_timeout(uint32_t current_time_ms, uint32_t timeout_ms = 50);

  /**
   * @brief Получить текущее состояние FSM
   */
  [[nodiscard]] ParserState get_state() const noexcept { return state_; }

private:
  ParserState state_{ParserState::WAIT_SYNC};
  DiagnosticsStats &stats_;
  FrameCallback on_frame_cb_;
  uint32_t last_byte_time_ms_{0};
  Frame rx_frame_{};
  size_t bytes_read_{0};
  uint8_t sync_index_{0};
  uint16_t rx_crc_{0};

  void transition_to(ParserState new_state) noexcept;
  [[nodiscard]] bool validate_crc() const noexcept;
};

} // namespace protocol
