#pragma once

#include "circular_buf.hpp"
#include <cstddef>
#include <cstdint>

// Forward-declaration структуры HAL, чтобы не подключать main.h в заголовок
struct __UART_HandleTypeDef;
typedef struct __UART_HandleTypeDef UART_HandleTypeDef;

namespace protocol {

class UartTxManager {
public:
  static constexpr size_t TX_QUEUE_SIZE = 1024;
  static constexpr size_t DMA_CHUNK_SIZE = 128;

  explicit UartTxManager(UART_HandleTypeDef &huart);

  // Запрет копирования
  UartTxManager(const UartTxManager &) = delete;
  UartTxManager &operator=(const UartTxManager &) = delete;

  /**
   * @brief Неблокирующий API отправки байт.
   */
  bool send_bytes(const uint8_t *data, size_t len);

  /**
   * @brief Вызывается из HAL_UART_TxCpltCallback при завершении отправки DMA.
   */
  void on_tx_complete_isr();

private:
  void start_dma_transmission();

  UART_HandleTypeDef &huart_;
  uint8_t tx_raw_buffer_[TX_QUEUE_SIZE];
  CircularBuffer tx_queue_;

  alignas(4) uint8_t dma_tx_chunk_[DMA_CHUNK_SIZE];
  volatile bool is_transmitting_{false};
};

} // namespace protocol
