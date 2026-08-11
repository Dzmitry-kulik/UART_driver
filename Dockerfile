# --- Этап 1: Сборка и тестирование (Builder & Tester Stage) ---
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Устанавливаем базовые системные утилиты, CMake, Python и зависимости Renode
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    make \
    python3 \
    python3-pip \
    python3-serial \
    wget \
    xz-utils \
    ca-certificates \
    mono-complete \
    gtk-sharp2 \
    && rm -rf /var/lib/apt/lists/*

# Скачиваем и устанавливаем официальный ARM GNU Toolchain
RUN wget -O /tmp/arm-toolchain.tar.xz https://developer.arm.com/-/media/Files/downloads/gnu/13.2.rel1/binrel/arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz && \
    mkdir -p /opt/arm-toolchain && \
    tar -xf /tmp/arm-toolchain.tar.xz -C /opt/arm-toolchain --strip-components=1 && \
    rm /tmp/arm-toolchain.tar.xz

# Скачиваем и устанавливаем стабильный Portable-релиз Renode
RUN wget https://builds.renode.io/renode-latest.linux-portable.tar.gz -O /tmp/renode.tar.gz && \
    mkdir -p /opt/renode && \
    tar -xzf /tmp/renode.tar.gz -C /opt/renode --strip-components=1 && \
    rm /tmp/renode.tar.gz

# Добавляем компилятор ARM и Renode в системный PATH
ENV PATH="/opt/arm-toolchain/bin:/opt/renode:$PATH"

WORKDIR /app
COPY . .

# 1. Сборка проекта (C++, Toolchain)
RUN cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build

# 2. Проверка размера прошивки через скрипт
RUN python3 scripts/check_size.py

# 3. Интеграционное тестирование: запуск Renode и прогон тестов
RUN renode --headless -e "include @tests/test_board.resc" & \
    sleep 3 && \
    python3 scripts/run_tests.py

# --- Этап 2: Подготовка артефактов (Artifacts Stage) ---
FROM scratch AS artifacts

# Копируем проверенный и собранный бинарник наружу
COPY --from=builder /app/build/UART_DRIVER /
