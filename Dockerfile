# --- Этап 1: Сборка и тестирование (Builder & Tester Stage) ---
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Устанавливаем базовые системные утилиты, CMake и распаковщики
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    make \
    python3 \
    python3-pip \
    wget \
    xz-utils \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Скачиваем и устанавливаем официальный ARM GNU Toolchain напрямую с сайта Arm
RUN wget -O /tmp/arm-toolchain.tar.xz https://developer.arm.com/-/media/Files/downloads/gnu/13.2.rel1/binrel/arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz && \
    mkdir -p /opt/arm-toolchain && \
    tar -xf /tmp/arm-toolchain.tar.xz -C /opt/arm-toolchain --strip-components=1 && \
    rm /tmp/arm-toolchain.tar.xz

# Добавляем компилятор в системный путь (PATH)
ENV PATH="/opt/arm-toolchain/bin:$PATH"

WORKDIR /app
COPY . .

# 1. Сборка проекта (C++, Toolchain)
RUN cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build

# 2. Проверка размера прошивки через скрипт
RUN python3 scripts/check_size.py

# --- Этап 2: Подготовка артефактов (Artifacts Stage) ---
FROM scratch AS artifacts

# Копируем проверенный и собранный ELF-файл наружу
COPY --from=builder /app/build/UART_DRIVER.elf /
