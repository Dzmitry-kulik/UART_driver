#include "tx_manager.hpp"
#include "main.h"

namespace protocol {

UartTxManager::UartTxManager(UART_HandleTypeDef &huart)
    : huart_(huart), tx_queue_(tx_raw_buffer_, TX_QUEUE_SIZE) {}

bool UartTxManager::send_bytes(const uint8_t *data, size_t len) {
  if (!data || len == 0) {
    return true;
  }

  for (size_t i = 0; i < len; ++i) {
    if (!tx_queue_.push(data[i])) {
      return false;
    }
  }

  __disable_irq();
  if (!is_transmitting_) {
    start_dma_transmission();
  }
  __enable_irq();

  return true;
}

void UartTxManager::on_tx_complete_isr() {
  is_transmitting_ = false;

  // Если в очереди остались данные, запускаем отправку следующего блока
  if (!tx_queue_.empty()) {
    start_dma_transmission();
  }
}

void UartTxManager::start_dma_transmission() {
  size_t chunk_size = 0;

  // Вычитываем данные из кольцевой очереди во временный линейный буфер
  while (chunk_size < DMA_CHUNK_SIZE &&
         tx_queue_.pop(dma_tx_chunk_[chunk_size])) {
    chunk_size++;
  }

  if (chunk_size > 0) {
    is_transmitting_ = true;

#ifdef CI_RENODE_TEST
    // Эмулятор Renode: отправка по прерываниям (IT)
    if (HAL_UART_Transmit_IT(&huart_, dma_tx_chunk_,
                             static_cast<uint16_t>(chunk_size)) != HAL_OK) {
      is_transmitting_ = false; // Сброс флага при сбое передачи
    }
#else
    // Реальное железо: отправка через DMA
    if (HAL_UART_Transmit_DMA(&huart_, dma_tx_chunk_,
                              static_cast<uint16_t>(chunk_size)) != HAL_OK) {
      is_transmitting_ = false; // Сброс флага при сбое передачи
    }
#endif
  }
}

} // namespace protocol
