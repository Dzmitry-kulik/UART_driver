/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.cpp
 * @brief          : Main program body (UART Receiver & Transmitter)
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "stm32f4xx_hal.h"

#undef CRC

#include "FSM_parser.hpp"
#include "crc16.hpp"
#include "diagnostics.hpp"
#include "frame.hpp"
#include "tx_manager.hpp"
#include <expected>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
constexpr size_t DMA_RX_BUFFER_SIZE = 1024;
constexpr size_t DMA_RX_BUFFER_MASK = DMA_RX_BUFFER_SIZE - 1;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
extern UART_HandleTypeDef huart1;

// Буферы приёма
alignas(4) uint8_t g_dma_rx_buffer[DMA_RX_BUFFER_SIZE];
size_t g_read_pos = 0;
uint8_t g_rx_renode_byte =
    0; // Временный байт для приёма по прерываниям в Renode

volatile bool g_data_received_event = false;

protocol::DiagnosticsStats g_stats{};
protocol::DiagnosticsService g_diagnostics(g_stats);
protocol::UartTxManager g_tx_manager(huart1);

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
extern "C" {
void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_DMA_Init(void);
void MX_USART1_UART_Init(void);
}

/* USER CODE BEGIN PFP */
inline uint16_t get_dma_rx_counter(void);
void on_frame_parsed(
    const std::expected<protocol::Frame, protocol::ParseError> &result);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

inline uint16_t get_dma_rx_counter(void) {
  return static_cast<uint16_t>(__HAL_DMA_GET_COUNTER(huart1.hdmarx));
}

void on_frame_parsed(
    const std::expected<protocol::Frame, protocol::ParseError> &result) {
  auto &stats = g_stats;

  if (result.has_value()) {
    const auto &frame = result.value();
    stats.rx_frames_ok++;

    static uint8_t ack_frame[9] = {
        0xAA, 0x55, // Преамбула
        0x01,       // Версия
        0x02,       // Тип (ACK)
        0x00,       // seq_num
        0x00, 0x00, // Длина payload = 0
        0x00, 0x00  // Место под CRC
    };

    ack_frame[4] = frame.seq_num;

    uint16_t crc = protocol::Crc16::calculate(&ack_frame[2], 5);

    ack_frame[7] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    ack_frame[8] = static_cast<uint8_t>(crc & 0xFF);

    g_tx_manager.send_bytes(ack_frame, sizeof(ack_frame));

    switch (frame.type) {
    case protocol::MessageType::DATA:
      break;
    case protocol::MessageType::ACK:
    case protocol::MessageType::NACK:
      break;
    default:
      break;
    }
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
/**
 * @brief Обработчик окончания отправки блока UART TX.
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART1) {
    g_tx_manager.on_tx_complete_isr();
  }
}

/**
 * @brief Обработчик окончания приёма байта UART RX (для режима IT в Renode).
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART1) {
#ifdef CI_RENODE_TEST
    g_parser.process_byte(g_rx_renode_byte);
    HAL_UART_Receive_IT(&huart1, &g_rx_renode_byte, 1);
#endif
  }
}

/**
 * @brief Обработчик аппаратных ошибок периферии UART.
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART1) {
    uint32_t er = huart->ErrorCode;

    if (er & HAL_UART_ERROR_FE)
      g_stats.hw_framing_errors++;
    if (er & HAL_UART_ERROR_PE)
      g_stats.hw_parity_errors++;
    if (er & HAL_UART_ERROR_ORE)
      g_stats.hw_overrun_errors++;

#ifdef CI_RENODE_TEST
    HAL_UART_Receive_IT(&huart1, &g_rx_renode_byte, 1);
#else
    HAL_UART_Receive_DMA(huart, g_dma_rx_buffer, DMA_RX_BUFFER_SIZE);
#endif
  }
}
}
/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();

  /* USER CODE BEGIN 2 */

  HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);

#ifdef CI_RENODE_TEST
  // Для Renode включаем приём по прерываниям
  HAL_UART_Receive_IT(&huart1, &g_rx_renode_byte, 1);
#else
  // На реальной железяке запускаем аппаратный кольцевой DMA
  HAL_UART_Receive_DMA(&huart1, g_dma_rx_buffer, DMA_RX_BUFFER_SIZE);
  __HAL_UART_CLEAR_IDLEFLAG(&huart1);
  __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
#endif

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
#ifndef CI_RENODE_TEST
    // На реальном железе данные вычитываются из кольцевого буфера DMA
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
#endif
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}
