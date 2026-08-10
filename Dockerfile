# --- Этап 1: Сборка и тестирование (Builder & Tester Stage) ---
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Устанавливаем ARM Toolchain, CMake, Make, Python и wget/curl для загрузки Renode
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    make \
    gcc-arm-none-eabi \
    g++-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    python3 \
    python3-pip \
    wget \
    dotnet-runtime-8.0 \
    libgtk2.0-0 \
    libglib2.0-0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

# 1. Сборка проекта (C++23, Toolchain)
RUN cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build

# 2. Проверка размера прошивки через скрипт
RUN python3 scripts/check_size.py

# 3. Запуск Unit-тестов парсера на Python
RUN python3 tools/parser_simulator.py

# --- Этап 2: Подготовка артефактов (Artifacts Stage) ---
FROM scratch AS artifacts

# Копируем проверенный и собранный ELF-файл наружу
COPY --from=builder /app/build/UART_DRIVER.elf /