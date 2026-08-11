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
#include "diagnostics.hpp"
#include "frame_parser.hpp"
#include "tx_manager.hpp"
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
// Хэндл UART1, сгенерированный STM32CubeMX
extern UART_HandleTypeDef huart1;

// Кольцевой буфер приёма DMA и программный индекс чтения
alignas(4) uint8_t g_dma_rx_buffer[DMA_RX_BUFFER_SIZE];
size_t g_read_pos = 0;

// Флаг события прерывания IDLE (выставляется в stm32f4xx_it.c)
volatile bool g_data_received_event = false;

// 1. Диагностический сервис статистики
protocol::DiagnosticsService g_diagnostics{};

// 2. Менеджер отправки через DMA TX с кольцевой очередью
protocol::UartTxManager g_tx_manager(huart1);

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART1_UART_Init(void);

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
  auto &stats = g_diagnostics.get_stats_mutable();

  if (result.has_value()) {
    const auto &frame = result.value();
    stats.rx_frames_ok++;

    // Бизнес-логика обработки валидного кадра
    switch (frame.type) {
    case protocol::MessageType::DATA:
      // Доступ к данным кадра: frame.payload
      break;

    case protocol::MessageType::ACK:
    case protocol::MessageType::NACK:
      break;

    default:
      break;
    }
  } else {
    // Учёт ошибок парсинга протокола
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
static protocol::FrameParser g_parser(g_diagnostics.get_stats_mutable(),
                                      on_frame_parsed);

/**
 * @brief Обработчик окончания отправки блока DMA TX.
 * Вызывается автоматически библиотека HAL при завершении передачи.
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART1) {
    g_tx_manager.on_tx_complete_isr();
  }
}

/**
 * @brief Обработчик аппаратных ошибок периферии UART.
 * Регистрирует Framing, Parity, Overrun ошибки в DiagnosticsService.
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART1) {
    uint32_t er = huart->ErrorCode;
    auto &stats = g_diagnostics.get_stats_mutable();

    if (er & HAL_UART_ERROR_FE) {
      stats.hw_framing_errors++;
    }
    if (er & HAL_UART_ERROR_PE) {
      stats.hw_parity_errors++;
    }
    if (er & HAL_UART_ERROR_ORE) {
      stats.hw_overrun_errors++;
      // В случае Overrun сбрасывается прием DMA, поэтому перезапускаем его
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

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick.
   */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();

  /* USER CODE BEGIN 2 */

  // 1. Старт приема DMA в цикличном режиме (CIRCULAR)
  HAL_UART_Receive_DMA(&huart1, g_dma_rx_buffer, DMA_RX_BUFFER_SIZE);

  // 2. Включаем прерывание по линии простоя (IDLE line)
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

        // Основной вызов FSM-парсера
        g_parser.process_byte(byte);

        // Продвигаем программный индекс чтения по битовой маске
        g_read_pos = (g_read_pos + 1) & DMA_RX_BUFFER_MASK;

        // Обновляем текущую позицию DMA на случай прихода новых байт
        dma_write_pos = DMA_RX_BUFFER_SIZE - get_dma_rx_counter();
      }
    }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
   */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/**
 * @brief USART1 Initialization Function
 */
static void MX_USART1_UART_Init(void) {
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK) {
    Error_Handler();
  }
}

/**
 * Enable DMA controller clock
 */
static void MX_DMA_Init(void) {
  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream2_IRQn interrupt configuration (USART1_RX) */
  HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);
}

/**
 * @brief GPIO Initialization Function
 */
static void MX_GPIO_Init(void) { __HAL_RCC_GPIOA_CLK_ENABLE(); }

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1) {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
