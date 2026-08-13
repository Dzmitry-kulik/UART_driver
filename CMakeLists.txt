cmake_minimum_required(VERSION 3.22)

project(UART_DRIVER C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Опция для включения сборки юнит-тестов с измерением покрытия (Coverage)
option(ENABLE_COVERAGE "Enable code coverage reporting for host unit tests" OFF)

if(ENABLE_COVERAGE)
    # ==============================================================================
    # РЕЖИМ 1: СБОРКА HOST UNIT-ТЕСТОВ С ПОКРЫТИЕМ (x86 GCC)
    # ==============================================================================
    add_executable(fsm_unit_tests
        tests/unit/test_fsm_parser.cpp
        Core/Src/protocol/src/FSM_parser.cpp
        Core/Src/protocol/src/diagnostics.cpp
        Core/Src/protocol/src/circular_buf.cpp
    )

    # Флаги профилирования gcov/gcovr изолированно для таргета
    target_compile_options(fsm_unit_tests PRIVATE --coverage -O0 -g3)
    target_link_options(fsm_unit_tests PRIVATE --coverage)

    # Активация макроса подмены HAL_GetTick на ПК
    target_compile_definitions(fsm_unit_tests PRIVATE HOST_UNIT_TEST)

    # Заголовочные файлы для хост-тестов
    target_include_directories(fsm_unit_tests PRIVATE
        Core/Inc
        Core/Src/protocol/inc
    )

else()
    # ==============================================================================
    # РЕЖИМ 2: СБОРКА ПРОШИВКИ ДЛЯ STM32F411CEU6 (ARM Cortex-M4)
    # ==============================================================================

    # Опции компиляции и линковки для STM32F411CEU6
    set(CPU_OPTIONS
        -mcpu=cortex-m4
        -mfpu=fpv4-sp-d16
        -mfloat-abi=hard
        -mthumb
    )

    # Объявляем таргет С РАСШИРЕНИЕМ .elf
    add_executable(UART_DRIVER.elf)

    # Автоматически находим все исходные файлы HAL-библиотеки с отслеживанием
    file(GLOB HAL_SOURCES CONFIGURE_DEPENDS "Drivers/STM32F4xx_HAL_Driver/Src/*.c")

    # Исходные файлы проекта
    target_sources(UART_DRIVER.elf PRIVATE
        # Основные системные файлы, пользовательские прерывания и стартовый файл
        Core/Src/main.cpp
        Core/Src/peripherals.c
        Core/Src/stm32f4xx_hal_msp.c
        Core/Src/stm32f4xx_it.c
        Core/Src/syscalls.c
        Core/Src/sysmem.c
        Core/Src/system_stm32f4xx.c
        Core/Startup/startup_stm32f411ceux.s

        # Файлы протокола
        Core/Src/protocol/src/circular_buf.cpp
        Core/Src/protocol/src/diagnostics.cpp
        Core/Src/protocol/src/FSM_parser.cpp
        Core/Src/protocol/src/tx_manager.cpp

        # Все исходники HAL
        ${HAL_SOURCES}
    )

    # Пути к заголовочным файлам
    target_include_directories(UART_DRIVER.elf PRIVATE
        Core/Inc
        Core/Src/protocol/inc
        Drivers/STM32F4xx_HAL_Driver/Inc
        Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
        Drivers/CMSIS/Device/ST/STM32F4xx/Include
        Drivers/CMSIS/Include
    )

    # Макросы препроцессора
    target_compile_definitions(UART_DRIVER.elf PRIVATE
        STM32F411xE
        USE_HAL_DRIVER
    )

    # Флаги компилятора (С ПОЛНЫМ ОТКЛЮЧЕНИЕМ ОПТИМИЗАЦИЙ ДЛЯ ОТЛАДКИ)
    target_compile_options(UART_DRIVER.elf PRIVATE
        ${CPU_OPTIONS}
        -O0
        -g3
        -Wno-unused-parameter
        -fdata-sections
        -ffunction-sections
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    )

    # Флаги линковщика
    target_link_options(UART_DRIVER.elf PRIVATE
        ${CPU_OPTIONS}
        -specs=nano.specs
        -T${CMAKE_CURRENT_SOURCE_DIR}/STM32F411CEUX_FLASH.ld
        -Wl,-Map=${PROJECT_NAME}.map,--cref
        -Wl,--gc-sections
        -Wl,--no-warn-rwx-segments
    )
endif()
