#pragma once

#include "circular_buf.hpp"
#include <cstddef>
#include <cstdint>

struct __UART_HandleTypeDef;
typedef struct __UART_HandleTypeDef UART_HandleTypeDef;

namespace protocol {

class UartTxManager {
public:
  static constexpr size_t TX_QUEUE_SIZE = 1024;
  static constexpr size_t DMA_CHUNK_SIZE = 128;
  static constexpr size_t MAX_PAYLOAD_SIZE = 256;

  explicit UartTxManager(UART_HandleTypeDef &huart);

  UartTxManager(const UartTxManager &) = delete;
  UartTxManager &operator=(const UartTxManager &) = delete;

  /**
   * @brief Неблокирующий API отправки сырых байт.
   */
  bool send_bytes(const uint8_t *data, size_t len);

  /**
   * @brief Отправка кадра БЕЗ ожидания ACK (Fire-and-Forget).
   */
  bool send_frame(uint8_t msg_type, uint8_t seq_num, const uint8_t *payload,
                  size_t len);

  /**
   * @brief Отправка кадра С ГАРАНТИЕЙ ДОСТАВКИ (Stop-and-Wait ARQ).
   * @param timeout_ms Время ожидания ACK в мс.
   * @param max_retries Максимальное число повторов передачи при отсутствии ACK.
   * @return true, если кадр успешно принят в работу.
   */
  bool send_frame_with_ack(uint8_t msg_type, uint8_t seq_num,
                           const uint8_t *payload, size_t len,
                           uint32_t timeout_ms = 150, uint8_t max_retries = 3);

  /**
   * @brief Фиксирует пришедший ACK с той стороны.
   */
  void on_ack_received(uint8_t seq_num);

  /**
   * @brief Проверяет таймауты и выполняет переотправку. Вызывается в main loop.
   */
  void process_timeouts(uint32_t current_tick);

  /**
   * @brief Возвращает true, если прямо сейчас идет ожидание ACK.
   */
  bool is_waiting_ack() const { return is_waiting_ack_; }

  /**
   * @brief Вызывается из HAL_UART_TxCpltCallback.
   */
  void on_tx_complete_isr();

private:
  void start_dma_transmission();

  UART_HandleTypeDef &huart_;
  uint8_t tx_raw_buffer_[TX_QUEUE_SIZE];
  CircularBuffer tx_queue_;

  alignas(4) uint8_t dma_tx_chunk_[DMA_CHUNK_SIZE];
  volatile bool is_transmitting_{false};

  // Поля для поддержки ARQ (повтора передачи)
  bool is_waiting_ack_{false};
  uint8_t pending_msg_type_{0};
  uint8_t pending_seq_num_{0};
  uint8_t pending_payload_[MAX_PAYLOAD_SIZE];
  size_t pending_payload_len_{0};
  uint32_t pending_last_send_time_{0};
  uint32_t pending_timeout_ms_{150};
  uint8_t pending_retries_left_{0};
};

} // namespace protocol
