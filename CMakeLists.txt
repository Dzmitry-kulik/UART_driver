cmake_minimum_required(VERSION 3.22)
project(UART_DRIVER C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CPU_FLAGS "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb")

add_compile_options(
    ${CPU_FLAGS}
    -Wall
    -Wextra
    -fdata-sections
    -ffunction-sections
    -fno-exceptions
    -fno-rtti
)

add_link_options(
    ${CPU_FLAGS}
    -T"${CMAKE_CURRENT_SOURCE_DIR}/STM32F411CEUX_FLASH.ld"
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -specs=nano.specs
    -specs=nosys.specs
)

set(SOURCES
    Core/Src/main.c
    Core/Src/stm32f4xx_hal_msp.c
    Core/Src/stm32f4xx_it.c
    Core/Src/syscalls.c
    Core/Src/sysmem.c
    Core/Src/system_stm32f4xx.c
    Core/Src/protocol/src/diagnostics.cpp
    Core/Src/protocol/src/FSM_parser.cpp
    Core/Src/protocol/src/reliable_transport.cpp
    Startup/startup_stm32f411ceux.s 
)

set(INCLUDES
    Core/Inc
    Core/Src/protocol/inc
    Drivers/STM32F4xx_HAL_Driver/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32F4xx/Include
    Drivers/CMSIS/Include
)
file(GLOB_RECURSE HAL_DRIVERS "Drivers/STM32F4xx_HAL_Driver/Src/*.c")

list(APPEND SOURCES ${HAL_DRIVERS})

add_executable(${PROJECT_NAME}.elf ${SOURCES} ${LINKER_SCRIPT})

target_include_directories(${PROJECT_NAME}.elf PRIVATE ${INCLUDES})

target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
    DEBUG
    USE_HAL_DRIVER
    STM32F411xE
)