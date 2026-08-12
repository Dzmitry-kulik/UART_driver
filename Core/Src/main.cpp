/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.cpp
 * @brief          : Main program body (C++ Entry Point)
 ******************************************************************************
 */
/* USER CODE END Header */

#include "main.h"
#include "stm32f4xx_hal.h"

#undef CRC

#include "FSM_parser.hpp"
#include "crc16.hpp"
#include "diagnostics.hpp"
#include "frame.hpp"
#include "tx_manager.hpp"
#include <expected>

// Объявляем функции инициализации, которые сгенерировал CubeMX (они написаны на
// C)
extern "C" {
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART1_UART_Init(void);
}

constexpr size_t DMA_RX_BUFFER_SIZE = 1024;
constexpr size_t DMA_RX_BUFFER_MASK = DMA_RX_BUFFER_SIZE - 1;

UART_HandleTypeDef huart1;
alignas(4) uint8_t g_dma_rx_buffer[DMA_RX_BUFFER_SIZE];
size_t g_read_pos = 0;
volatile bool g_data_received_event = false;

protocol::DiagnosticsStats g_stats{};
protocol::DiagnosticsService g_diagnostics(g_stats);
protocol::UartTxManager g_tx_manager(huart1);

inline uint16_t get_dma_rx_counter(void) {
  return static_cast<uint16_t>(__HAL_DMA_GET_COUNTER(huart1.hdmarx));
}

void on_frame_parsed(
    const std::expected<protocol::Frame, protocol::ParseError> &result) {
  auto &stats = g_stats;

  if (result.has_value()) {
    const auto &frame = result.value();
    stats.rx_frames_ok++;

    uint8_t ack_frame[9] = {0xAA, 0x55, 0x01, 0x02, 0x00,
                            0x00, 0x00, 0x00, 0x00};

    ack_frame[4] = frame.seq_num;
    uint16_t crc = protocol::Crc16::calculate(&ack_frame[2], 5);

    ack_frame[7] = (crc >> 8) & 0xFF;
    ack_frame[8] = crc & 0xFF;

    g_tx_manager.send_bytes(ack_frame, sizeof(ack_frame));
  } else {
    switch (result.error()) {
    case protocol::ParseError::INVALID_CRC:
      stats.crc_errors++;
      break;
    case protocol::ParseError::PAYLOAD_TOO_LARGE:
      stats.length_errors++;
      break;
    default:
      stats.resync_events++;
      break;
    }
  }
}

static protocol::FrameParser g_parser(g_stats, on_frame_parsed);

extern "C" {
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART1) {
    g_tx_manager.on_tx_complete_isr();
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART1) {
    uint32_t er = huart->ErrorCode;
    if (er & HAL_UART_ERROR_FE)
      g_stats.hw_framing_errors++;
    if (er & HAL_UART_ERROR_PE)
      g_stats.hw_parity_errors++;
    if (er & HAL_UART_ERROR_ORE)
      g_stats.hw_overrun_errors++;

    HAL_UART_Receive_DMA(huart, g_dma_rx_buffer, DMA_RX_BUFFER_SIZE);
  }
}
}

int main(void) {
  HAL_Init();
  SystemClock_Config();

  // Инициализация из CubeMX
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();

  // Настройка прерываний и запуск DMA
  HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);

  HAL_UART_Receive_DMA(&huart1, g_dma_rx_buffer, DMA_RX_BUFFER_SIZE);
  __HAL_UART_CLEAR_IDLEFLAG(&huart1);
  __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);

  while (1) {
    size_t dma_write_pos = DMA_RX_BUFFER_SIZE - get_dma_rx_counter();

    if (g_data_received_event || (g_read_pos != dma_write_pos)) {
      g_data_received_event = false;

      while (g_read_pos != dma_write_pos) {
        uint8_t byte = g_dma_rx_buffer[g_read_pos];
        g_parser.process_byte(byte);
        g_read_pos = (g_read_pos + 1) & DMA_RX_BUFFER_MASK;
        dma_write_pos = DMA_RX_BUFFER_SIZE - get_dma_rx_counter();
      }
    }
  }
}
