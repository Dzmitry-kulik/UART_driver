/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.cpp
 * @brief          : Main program body (UART Receiver & Transmitter)
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
#include <cstring> // Для std::memcpy
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

alignas(4) uint8_t g_dma_rx_buffer[DMA_RX_BUFFER_SIZE];
volatile size_t g_read_pos = 0;

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
void renode_process_rx_byte(uint8_t byte);
}

/* USER CODE BEGIN PFP */
inline uint16_t get_dma_rx_counter(void);
inline void process_dma_rx_bytes(void);
void on_frame_parsed(
    const std::expected<protocol::Frame, protocol::ParseError> &result);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static const char LOREM_IPSUM[] =
    R"(orem ipsum dolor sit amet, consectetur adipiscing elit. Sed molestie
sit amet tellus sed fringilla. Integer efficitur urna augue, ut
consequat enim pellentesque sed. Quisque venenatis, tellus in vestibulum
 volutpat, sapien risus varius massa, id tincidunt enim sem eu tortor.
Morbi aliquam pulvinar varius. Mauris rutrum nibh magna, at ultricies
erat convallis vitae. In vehicula erat sit amet felis maximus, at
sagittis massa semper. Proin pretium volutpat urna, quis accumsan nulla
tincidunt eget. Donec congue dolor sollicitudin purus rhoncus elementum.
 Aenean nec magna facilisis, dignissim odio ac, euismod velit. Donec in
tincidunt nisl, ut consectetur ante. Phasellus quis eros id lorem
sodales lacinia ac nec nunc. Aenean tincidunt quam eu volutpat
ullamcorper. Curabitur ullamcorper ipsum a mi tempor dictum. Donec sit
amet sollicitudin justo. Duis in diam eget neque bibendum malesuada ut
eget massa. Donec dictum quam ut pretium pharetra.

Phasellus non porttitor nibh. Praesent tincidunt luctus lectus. Nulla
lacinia imperdiet enim, vulputate pellentesque ipsum mollis sit amet.
Maecenas cursus mi at posuere lacinia. Vestibulum ante ipsum primis in
faucibus orci luctus et ultrices posuere cubilia curae; Suspendisse
convallis turpis vel tellus maximus aliquet. Sed magna velit, tempor eu
egestas nec, porttitor eu neque. Quisque condimentum, arcu luctus
placerat lacinia, tellus arcu tempor urna, sit amet posuere magna dui
non metus. Duis euismod magna dui, non scelerisque nulla mattis ut. Duis
 in consectetur justo. Aenean gravida mauris metus, venenatis fringilla
metus suscipit sed. Pellentesque et fermentum felis, quis gravida nunc.

Nam rhoncus ullamcorper ligula. Praesent eu tempor quam. Suspendisse vel
 odio pellentesque, luctus nunc molestie, varius neque. Quisque
efficitur venenatis cursus. Donec at venenatis nisl. In maximus est
tempus dui luctus euismod. Maecenas porta, sem sit amet interdum
viverra, arcu ex rhoncus odio, non dapibus dui felis at risus.

Curabitur vel dignissim tellus. Aenean id pellentesque diam. Donec
lobortis leo non odio euismod, eu facilisis ante porta. Integer aliquet
placerat elit eget rutrum. Etiam a orci et eros porttitor lobortis ac et
 justo. Sed dapibus magna non risus euismod, quis vestibulum turpis
blandit. Duis ut feugiat est. Aliquam tempor, sapien non pharetra
finibus, dolor dolor aliquet est, nec malesuada ante nisl blandit sem.
Vivamus vel pellentesque ligula, quis commodo mi. Sed venenatis lacinia
metus id placerat. Integer vel turpis enim. Donec vulputate id nisl eget
 iaculis.

Curabitur at felis justo. Ut metus lacus, mattis id mollis vitae,
facilisis id enim. Aliquam ullamcorper faucibus lectus gravida rhoncus.
Donec maximus felis vel arcu imperdiet, a mattis augue interdum.
Suspendisse placerat, turpis ut tincidunt mattis, dui sapien convallis
erat, id congue est elit vel tortor. Pellentesque accumsan id ipsum eget
 auctor. Ut vestibulum viverra lorem vel imperdiet. Vestibulum sed
vehicula lectus, id volutpat mi. In imperdiet arcu suscipit maximus
pretium. Cras sed auctor diam. Maecenas ultricies in ligula vitae
faucibus)";

inline uint16_t get_dma_rx_counter(void) {
  return static_cast<uint16_t>(__HAL_DMA_GET_COUNTER(huart1.hdmarx));
}

/**
 * @brief Функция вычитания байт из кольцевого буфера DMA
 */
inline void process_dma_rx_bytes(void) {
#ifndef CI_RENODE_TEST
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
}

/**
 * @brief Попотоковая отправка текста с контролем ARQ (ожидание ACK)
 */
void send_lorem_ipsum_stream(protocol::UartTxManager &tx_mgr) {
  static uint8_t seq_num = 0;
  constexpr size_t CHUNK_SIZE = 256;
  constexpr uint8_t MSG_TYPE_DATA = 0x01;

  const size_t total_len = sizeof(LOREM_IPSUM) - 1;
  size_t offset = 0;

  while (offset < total_len) {
    size_t bytes_to_send =
        (total_len - offset > CHUNK_SIZE) ? CHUNK_SIZE : (total_len - offset);
    const uint8_t *chunk_ptr =
        reinterpret_cast<const uint8_t *>(LOREM_IPSUM + offset);

    // 1. Пытаемся отправить кадр с контролем доставки (таймаут 150мс, 3
    // попытки)
    while (!tx_mgr.send_frame_with_ack(MSG_TYPE_DATA, seq_num, chunk_ptr,
                                       bytes_to_send, 150, 3)) {
      tx_mgr.process_timeouts(HAL_GetTick());
      process_dma_rx_bytes();
    }

    // 2. Ждем, пока кадр будет успешно подтвержден (или сброшен по превышению
    // попыток)
    while (tx_mgr.is_waiting_ack()) {
      tx_mgr.process_timeouts(HAL_GetTick());
      process_dma_rx_bytes();
    }

    seq_num++;
    offset += bytes_to_send;
  }
}

/**
 * @brief Неблокирующая проверка нажатия кнопки с антидребезгом (50 мс).
 */
bool is_button_pressed_debounced() {
  static uint32_t last_change_time = 0;
  static GPIO_PinState last_state = GPIO_PIN_SET;
  static bool button_handled = false;

  GPIO_PinState current_state = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);
  if (current_state != last_state) {
    last_change_time = HAL_GetTick();
    last_state = current_state;
  }

  if ((HAL_GetTick() - last_change_time) > 50) {
    if (current_state == GPIO_PIN_RESET) {
      if (!button_handled) {
        button_handled = true;
        return true;
      }
    } else {
      button_handled = false;
    }
  }
  return false;
}

void on_frame_parsed(
    const std::expected<protocol::Frame, protocol::ParseError> &result) {
  if (!result.has_value())
    return;

  const auto &frame = result.value();
  if (frame.version != 0x01)
    return;

  g_stats.rx_frames_ok++;

  uint8_t type_val = static_cast<uint8_t>(frame.type);

  // 1. Пришли данные (DATA) -> отвечаем ACK
  if (type_val == 0x01) {
    uint8_t ack_frame[9] = {0xAA,          0x55, 0x01,
                            0x02, // MSG_TYPE_ACK
                            frame.seq_num, 0x00, 0x00, 0x00, 0x00};

    uint16_t crc = protocol::Crc16::calculate(&ack_frame[2], 5);
    ack_frame[7] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    ack_frame[8] = static_cast<uint8_t>(crc & 0xFF);

    g_tx_manager.send_bytes(ack_frame, sizeof(ack_frame));
  }
  // 2. Пришел ACK -> передаем его менеджеру
  else if (type_val == 0x02) {
    g_tx_manager.on_ack_received(frame.seq_num);
  }
  // 3. Запрос телеметрии (GET_STATS) от Python-теста
  else if (type_val == 0x10) {
    constexpr size_t stats_len = sizeof(protocol::DiagnosticsStats);
    constexpr size_t frame_len = 9 + stats_len;

    uint8_t resp_frame[frame_len] = {0};
    resp_frame[0] = 0xAA;
    resp_frame[1] = 0x55;
    resp_frame[2] = 0x01;
    resp_frame[3] = 0x11; // STATS_RESP
    resp_frame[4] = frame.seq_num;
    resp_frame[5] = static_cast<uint8_t>((stats_len >> 8) & 0xFF);
    resp_frame[6] = static_cast<uint8_t>(stats_len & 0xFF);

    std::memcpy(&resp_frame[7], &g_stats, stats_len);

    uint16_t crc = protocol::Crc16::calculate(&resp_frame[2], 5 + stats_len);
    resp_frame[frame_len - 2] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    resp_frame[frame_len - 1] = static_cast<uint8_t>(crc & 0xFF);

    g_tx_manager.send_bytes(resp_frame, frame_len);
  }
}
static protocol::FrameParser g_parser(g_stats, on_frame_parsed);

extern "C" {
void renode_process_rx_byte(uint8_t byte) { g_parser.process_byte(byte); }

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

#ifndef CI_RENODE_TEST
    HAL_UART_Receive_DMA(huart, (uint8_t *)g_dma_rx_buffer, DMA_RX_BUFFER_SIZE);
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
  (void)g_read_pos;

  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();

  /* USER CODE BEGIN 2 */
  HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);

#ifdef CI_RENODE_TEST
  __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
#else
  HAL_UART_Receive_DMA(&huart1, (uint8_t *)g_dma_rx_buffer, DMA_RX_BUFFER_SIZE);
  __HAL_UART_CLEAR_IDLEFLAG(&huart1);
  __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
#endif
  /* USER CODE END 2 */

  /* Infinite loop */
  while (1) {
    // 1. Проверяем таймауты ACK
    g_tx_manager.process_timeouts(HAL_GetTick());

#ifndef CI_RENODE_TEST
    // 2. Реакция на кнопку
    if (is_button_pressed_debounced()) {
      send_lorem_ipsum_stream(g_tx_manager);
    }
#endif

    // 3. Таймаут FSM парсера
    g_parser.check_timeout(HAL_GetTick(), 200);

    // 4. Вычитка входящих байт
    process_dma_rx_bytes();
  }
}
