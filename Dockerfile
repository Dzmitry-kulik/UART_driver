# --- Этап 1: Сборка и тестирование (Builder & Tester Stage) ---
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Устанавливаем базовые системные утилиты, gcc-12/g++-12 (для C++23 std::expected),
# утилиты для покрытия (gcovr), CMake, Python, зависимости Renode и gdb-multiarch
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    make \
    gcc-12 \
    g++-12 \
    python3 \
    python3-pip \
    python3-serial \
    wget \
    xz-utils \
    ca-certificates \
    mono-complete \
    gtk-sharp2 \
    gdb-multiarch \
    gcovr \
    && rm -rf /var/lib/apt/lists/*

# Создаем системные ссылки, чтобы gcc, g++ и gcov по умолчанию указывали на версию 12
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 100 \
    --slave /usr/bin/g++ g++ /usr/bin/g++-12 \
    --slave /usr/bin/gcov gcov /usr/bin/gcov-12

ENV CC=gcc-12 CXX=g++-12

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

# ==============================================================================
# ШАГ 1: Запуск Host Unit-тестов и проверка Code Coverage (>= 80%)
# ==============================================================================
# Добавлен параметр --gcov-executable gcov-12 для явного указания версии gcov
RUN cmake -B build_host -DENABLE_COVERAGE=ON && \
    cmake --build build_host && \
    ./build_host/fsm_unit_tests && \
    gcovr --gcov-executable gcov-12 -r . --filter "Core/Src/protocol/src/FSM_parser.cpp" --fail-under-line 80

# ==============================================================================
# ШАГ 2: Сборка прошивки для STM32 (ARM)
# ==============================================================================
RUN cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-DCI_RENODE_TEST" -DCMAKE_C_FLAGS="-DCI_RENODE_TEST" && \
    cmake --build build

# Проверка размера прошивки через скрипт из папки tests/
RUN python3 tests/check_size.py

# ==============================================================================
# ШАГ 3: Интеграционный прогон: Запуск Renode + Автоматический дамп через gdb-multiarch
# ==============================================================================
RUN renode --disable-xwt tests/test_board.resc & PID=$! && \
    sleep 5 && \
    python3 tests/tests_renode.py --url socket://localhost:4321 --timeout 0.5 ; \
    TEST_RESULT=$? ; \
    gdb-multiarch build/UART_DRIVER.elf -batch -x tests/ci_debug.gdb ; \
    kill $PID ; \
    exit $TEST_RESULT

# --- Этап 2: Подготовка артефактов (Artifacts Stage) ---
FROM scratch AS artifacts

# Копируем проверенный и собранный бинарник наружу
COPY --from=builder /app/build/UART_DRIVER.elf /
