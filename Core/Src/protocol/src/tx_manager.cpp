#include "tx_manager.hpp"
#include "main.h"

namespace protocol {

namespace {

constexpr uint8_t PREAMBLE[2] = {0xAA, 0x55};
constexpr uint8_t PROTOCOL_VERSION = 0x01;

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

  // 1. Помещаем байты в очередь
  for (size_t i = 0; i < len; ++i) {
    if (!tx_queue_.push(data[i])) {
      return false;
    }
  }

  // 2. Безопасно проверяем и взводим флаг передачи
  bool start_tx = false;
  __disable_irq();
  if (!is_transmitting_) {
    is_transmitting_ = true;
    start_tx = true;
  }
  __enable_irq();

  // 3. Запускаем физическую передачу ВНЕ критической секции,
  // чтобы не блокировать системный таймер (SysTick)
  if (start_tx) {
    start_dma_transmission();
  }

  return true;
}

bool UartTxManager::send_frame(uint8_t msg_type, uint8_t seq_num,
                               const uint8_t *payload, size_t len) {
  uint8_t header[5] = {PROTOCOL_VERSION, msg_type, seq_num,
                       static_cast<uint8_t>((len >> 8) & 0xFF),
                       static_cast<uint8_t>(len & 0xFF)};

  uint16_t crc = calculate_crc16_step(0xFFFF, header, sizeof(header));
  if (payload && len > 0) {
    crc = calculate_crc16_step(crc, payload, len);
  }

  uint8_t crc_bytes[2] = {static_cast<uint8_t>((crc >> 8) & 0xFF),
                          static_cast<uint8_t>(crc & 0xFF)};

  if (!send_bytes(PREAMBLE, sizeof(PREAMBLE)))
    return false;
  if (!send_bytes(header, sizeof(header)))
    return false;
  if (payload && len > 0) {
    if (!send_bytes(payload, len))
      return false;
  }
  if (!send_bytes(crc_bytes, sizeof(crc_bytes)))
    return false;

  return true;
}

bool UartTxManager::send_frame_with_ack(uint8_t msg_type, uint8_t seq_num,
                                        const uint8_t *payload, size_t len,
                                        uint32_t timeout_ms,
                                        uint8_t max_retries) {
  if (is_waiting_ack_) {
    return false;
  }

  if (len > MAX_PAYLOAD_SIZE) {
    return false;
  }

  pending_msg_type_ = msg_type;
  pending_seq_num_ = seq_num;
  pending_payload_len_ = len;
  if (payload && len > 0) {
    for (size_t i = 0; i < len; ++i) {
      pending_payload_[i] = payload[i];
    }
  }
  pending_timeout_ms_ = timeout_ms;
  pending_retries_left_ = max_retries;

  if (!send_frame(msg_type, seq_num, payload, len)) {
    return false;
  }

  is_waiting_ack_ = true;
  pending_last_send_time_ = HAL_GetTick();

  return true;
}

void UartTxManager::on_ack_received(uint8_t seq_num) {
  if (is_waiting_ack_ && seq_num == pending_seq_num_) {
    is_waiting_ack_ = false;
  }
}

void UartTxManager::process_timeouts(uint32_t current_tick) {
  if (!is_waiting_ack_) {
    return;
  }

  if (current_tick - pending_last_send_time_ >= pending_timeout_ms_) {
    if (pending_retries_left_ > 0) {
      pending_retries_left_--;
      send_frame(pending_msg_type_, pending_seq_num_, pending_payload_,
                 pending_payload_len_);
      pending_last_send_time_ = current_tick;
    } else {
      is_waiting_ack_ = false;
    }
  }
}

void UartTxManager::on_tx_complete_isr() {
  is_transmitting_ = false;

  if (!tx_queue_.empty()) {
    is_transmitting_ = true;
    start_dma_transmission();
  }
}

void UartTxManager::start_dma_transmission() {
  size_t chunk_size = 0;

  while (chunk_size < DMA_CHUNK_SIZE &&
         tx_queue_.pop(dma_tx_chunk_[chunk_size])) {
    chunk_size++;
  }

  if (chunk_size > 0) {
#ifdef CI_RENODE_TEST
    // ЭМУЛЯТОР: Полностью обходим HAL и пишем напрямую в регистр данных (DR).
    // Это гарантирует, что статус BUSY_TX не заблокирует передачу пакета.
    for (size_t i = 0; i < chunk_size; ++i) {
      // Ждем, пока аппаратный буфер передачи освободится
      while (__HAL_UART_GET_FLAG(&huart_, UART_FLAG_TXE) == RESET) {
        // Ожидание установки флага
      }
      // Пишем байт напрямую в регистр передачи UART
      huart_.Instance->DR = (dma_tx_chunk_[i] & 0xFF);
    }

    // Мгновенно снимаем флаг передачи для текущего блока
    is_transmitting_ = false;

    // Рекурсивно выталкиваем остатки, если очередь не пуста
    if (!tx_queue_.empty()) {
      is_transmitting_ = true;
      start_dma_transmission();
    }
#else
    // ЖЕЛЕЗО: Оставляем быструю асинхронную отправку через DMA
    if (HAL_UART_Transmit_DMA(&huart_, dma_tx_chunk_,
                              static_cast<uint16_t>(chunk_size)) != HAL_OK) {
      is_transmitting_ = false;
    }
#endif
  } else {
    is_transmitting_ = false;
  }
}

} // namespace protocol
