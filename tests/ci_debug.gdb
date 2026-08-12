# ==============================================================================
# GDB Batch Script for Renode Integration & Diagnostics
# Target: STM32F411 / UART DMA Driver
# ==============================================================================

# 1. Настройка вывода GDB для читаемого лога в CI/CD
set pagination off
set print pretty on
set print demangle on
set logging file build/gdb_execution.log
set logging on

# 2. Подключение к GDB-серверу Renode
target remote localhost:3333

# 3. Перезагрузка MCU и остановка на старте
monitor reset halt
load

# ==============================================================================
# BREAKPOINTS & COMMANDS
# ==============================================================================

# --- Брейкпоинт 1: Успешный обработчик распарсенного кадра ---
break on_frame_parsed
commands 1
  silent
  echo \n=================== [GDB: FRAME PARSED] ===================\n
  echo >>> Stat: Frames OK:
  print g_stats.rx_frames_ok
  echo >>> Stat: CRC Errors:
  print g_stats.crc_errors
  echo >>> Stat: Length Errors:
  print g_stats.length_errors
  echo >>> Stat: Resync Events:
  print g_stats.resync_events
  echo >>> Current Read Pos:
  print g_read_pos
  continue
end

# --- Брейкпоинт 2: Перехват аппаратных ошибок UART (Overrun, Framing, Parity) ---
break HAL_UART_ErrorCallback
commands 2
  echo \n------------------ [GDB: HARDWARE ERROR DETECTED] ------------------\n
  echo >>> Error Code Flags:
  print/x huart->ErrorCode
  echo >>> Hardware Overrun Errors:
  print g_stats.hw_overrun_errors
  echo >>> Hardware Framing Errors:
  print g_stats.hw_framing_errors
  continue
end

# --- Брейкпоинт 3: Перехват критических сбоев MCU ---
break Error_Handler
commands 3
  echo \n🚨🚨🚨 [GDB FATAL: Error_Handler Reached!] 🚨🚨🚨\n
  echo >>> Call stack backtrace:\n
  backtrace
  echo \n>>> Registers snapshot:\n
  info registers
  echo \n>>> Final Stats:\n
  print g_stats
  quit 1
end

# --- Брейкпоинт 4: Завершение главного цикла main (на случай выхода из while(1)) ---
break main.cpp:215
commands 4
  echo \n[GDB] Main loop finished unexpected.\n
  quit 0
end

# ==============================================================================
# EXECUTION CONTROL
# ==============================================================================

# Запускаем прошивку на выполнение
continue

# Дамп состояния глобальных объектов при остановке по таймауту/сигналу
echo \n=================== [GDB: FINAL STATE DUMP] ===================\n
echo >>> Protocol Diagnostics Stats:\n
print g_stats

echo >>> DMA Buffer Read Position:
print g_read_pos

echo >>> DMA Buffer Write Position (calculated):
print 1024 - huart1.hdmarx->Instance->NDTR

echo >>> First 32 bytes of DMA Buffer:\n
x/32xb g_dma_rx_buffer

quit 0
