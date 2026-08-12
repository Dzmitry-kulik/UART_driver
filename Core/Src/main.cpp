/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.cpp
 * @brief          : Main program body (UART DMA Receiver & Transmitter with
 *Diagnostics)
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

// 1. Подключаем HAL для определения типов и макросов периферии
#include "stm32f4xx_hal.h"

// 2. Отменяем сишный макрос CRC библиотеки HAL, чтобы не ломать enum class
#undef CRC

// 3. C++ заголовки проекта
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
// Ссылаемся на хэндл UART1 из peripherals.c
extern UART_HandleTypeDef huart1;

// Кольцевой буфер приёма DMA и программный индекс чтения
alignas(4) uint8_t g_dma_rx_buffer[DMA_RX_BUFFER_SIZE];
size_t g_read_pos = 0;

// Флаг события прерывания IDLE (выставляется в stm32f4xx_it.c)
volatile bool g_data_received_event = false;

// 1. Создаем экземпляр структуры статистики, передаваемый в диагностику
protocol::DiagnosticsStats g_stats{};

// 2. Диагностический сервис статистики (принимает ссылку на stats)
protocol::DiagnosticsService g_diagnostics(g_stats);

// 3. Менеджер отправки через DMA TX с кольцевой очередью
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

/**
 * @brief Возвращает текущее значение счетчика NDTR (сколько байт осталось
 * принять DMA).
 */
inline uint16_t get_dma_rx_counter(void) {
  return static_cast<uint16_t>(__HAL_DMA_GET_COUNTER(huart1.hdmarx));
}

/**
 * @brief Callback, вызываемый FrameParser при разборе кадра или ошибке.
 */
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
        0x00,       // seq_num (заполним ниже)
        0x00, 0x00, // Длина payload = 0
        0x00, 0x00  // Место под CRC
    };

    // Обновляем sequence number под текущий кадр
    ack_frame[4] = frame.seq_num;

    // Считаем CRC16 для заголовка (5 байт)
    uint16_t crc = protocol::Crc16::calculate(&ack_frame[2], 5);

    // Записываем CRC (Big-Endian)
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

// Инициализация экземпляра FrameParser
static protocol::FrameParser g_parser(g_stats, on_frame_parsed);

extern "C" {
/**
 * @brief Обработчик окончания отправки блока DMA TX.
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART1) {
    g_tx_manager.on_tx_complete_isr();
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

    // Перезапуск приема DMA при ошибках
    HAL_UART_Receive_DMA(huart, g_dma_rx_buffer, DMA_RX_BUFFER_SIZE);
  }
}
}
/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {
  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick.
   */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();

  /* USER CODE BEGIN 2 */

  // Настройка прерываний UART
  HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);

  // 1. Старт приема DMA в цикличном режиме (CIRCULAR)
  HAL_UART_Receive_DMA(&huart1, g_dma_rx_buffer, DMA_RX_BUFFER_SIZE);

  // Сбрасываем флаг и включаем прерывание по линии простоя (IDLE line)
  __HAL_UART_CLEAR_IDLEFLAG(&huart1);
  __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    // Рассчитываем текущую позицию записи DMA в кольцевом буфере
    size_t dma_write_pos = DMA_RX_BUFFER_SIZE - get_dma_rx_counter();

    if (g_data_received_event || (g_read_pos != dma_write_pos)) {
      g_data_received_event = false;

      // Побайтовый разбор данных из DMA-буфера через класс FrameParser
      while (g_read_pos != dma_write_pos) {
        uint8_t byte = g_dma_rx_buffer[g_read_pos];
        g_parser.process_byte(byte);
        g_read_pos = (g_read_pos + 1) & DMA_RX_BUFFER_MASK;
        dma_write_pos = DMA_RX_BUFFER_SIZE - get_dma_rx_counter();
      }
    }

#ifdef CI_RENODE_TEST
    // --- СТРАХОВКА ДЛЯ RENODE ---
    // Если виртуальный DMA проигнорировал данные, вычитываем их вручную
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE)) {
      uint8_t byte = static_cast<uint8_t>(huart1.Instance->DR & 0x00FF);
      g_parser.process_byte(byte);
    }
#endif
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
}
/* USER CODE END 3 */
