#include "tx_manager.hpp"
#include "main.h"

namespace protocol {

namespace {

// Константы протокола
constexpr uint8_t PREAMBLE[2] = {0xAA, 0x55};
constexpr uint8_t PROTOCOL_VERSION = 0x01;

/**
 * @brief Побайтовый расчёт CRC16-CCITT (Poly: 0x1021, Init: 0xFFFF)
 */
uint16_t calculate_crc16_step(uint16_t crc, const uint8_t *data, size_t len) {
  if (!data)
    return crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int j = 0; j < 8; ++j) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

} // namespace

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

bool UartTxManager::send_frame(uint8_t msg_type, uint8_t seq_num,
                               const uint8_t *payload, size_t len) {
  // 1. Собираем заголовок (5 байт): Version (1B) + MsgType (1B) + SeqNum (1B) +
  // PayloadLength (2B Big-Endian)
  uint8_t header[5] = {
      PROTOCOL_VERSION, msg_type, seq_num,
      static_cast<uint8_t>((len >> 8) & 0xFF), // Length High
      static_cast<uint8_t>(len & 0xFF)         // Length Low
  };

  // 2. Вычисляем CRC16 от заголовка и полезной нагрузки
  uint16_t crc = calculate_crc16_step(0xFFFF, header, sizeof(header));
  if (payload && len > 0) {
    crc = calculate_crc16_step(crc, payload, len);
  }

  uint8_t crc_bytes[2] = {
      static_cast<uint8_t>((crc >> 8) & 0xFF), // CRC High
      static_cast<uint8_t>(crc & 0xFF)         // CRC Low
  };

  // 3. Последовательно помещаем все части кадра в очередь передачи
  if (!send_bytes(PREAMBLE, sizeof(PREAMBLE))) {
    return false;
  }
  if (!send_bytes(header, sizeof(header))) {
    return false;
  }
  if (payload && len > 0) {
    if (!send_bytes(payload, len)) {
      return false;
    }
  }
  if (!send_bytes(crc_bytes, sizeof(crc_bytes))) {
    return false;
  }

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
