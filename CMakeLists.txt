cmake_minimum_required(VERSION 3.22)

project(UART_DRIVER C CXX ASM)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m4)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Опции компиляции и линковки для STM32F411CEU6
set(CPU_OPTIONS
    -mcpu=cortex-m4
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -mthumb
)

add_executable(UART_DRIVER)

# Исходные файлы проекта
target_sources(UART_DRIVER PRIVATE
    # Основные системные файлы, HAL и стартовый файл (находятся в Core/Src)
    Core/Src/main.cpp
    Core/Src/stm32f4xx_hal_msp.c
    Core/Src/stm32f4xx_it.c
    Core/Src/syscalls.c
    Core/Src/sysmem.c
    Core/Src/system_stm32f4xx.c
    Core/Startup/startup_stm32f411ceux.s

    # Файлы протокола (внутри Core/Src/protocol/src)
    Core/Src/protocol/src/circular_buf.cpp
    Core/Src/protocol/src/diagnostics.cpp
    Core/Src/protocol/src/FSM_parser.cpp
    Core/Src/protocol/src/tx_manager.cpp
)

# Пути к заголовочным файлам
target_include_directories(UART_DRIVER PRIVATE
    Core/Inc
    Core/Src/protocol/inc
    Drivers/STM32F4xx_HAL_Driver/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32F4xx/Include
    Drivers/CMSIS/Include
)

# Макросы препроцессора
target_compile_definitions(UART_DRIVER PRIVATE
    STM32F411xE
    USE_HAL_DRIVER
)

# Флаги компилятора
target_compile_options(UART_DRIVER PRIVATE
    ${CPU_OPTIONS}
    -Wall
    -Wextra
    -Wno-unused-parameter
    -fdata-sections
    -ffunction-sections
)

# Флаги линковщика
target_link_options(UART_DRIVER PRIVATE
    ${CPU_OPTIONS}
    -specs=nano.specs
    -specs=nosys.specs
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32F411CEUX_FLASH.ld
    -Wl,-Map=${PROJECT_NAME}.map,--cref
    -Wl,--gc-sections
)
