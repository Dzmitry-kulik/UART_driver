# --- Этап 1: Сборка и тестирование (Builder & Tester Stage) ---
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Устанавливаем системные пакеты, g++ для хост-тестов и gcovr для отчета покрытия
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    make \
    g++ \
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

# Скачиваем и устанавливаем ARM GNU Toolchain
RUN wget -O /tmp/arm-toolchain.tar.xz https://developer.arm.com/-/media/Files/downloads/gnu/13.2.rel1/binrel/arm-gnu-toolchain-13.2.rel1-x86_64-arm-none-eabi.tar.xz && \
    mkdir -p /opt/arm-toolchain && \
    tar -xf /tmp/arm-toolchain.tar.xz -C /opt/arm-toolchain --strip-components=1 && \
    rm /tmp/arm-toolchain.tar.xz

# Скачиваем и устанавливаем Renode
RUN wget https://builds.renode.io/renode-latest.linux-portable.tar.gz -O /tmp/renode.tar.gz && \
    mkdir -p /opt/renode && \
    tar -xzf /tmp/renode.tar.gz -C /opt/renode --strip-components=1 && \
    rm /tmp/renode.tar.gz

# Добавляем Toolchain и Renode в системный PATH
ENV PATH="/opt/arm-toolchain/bin:/opt/renode:$PATH"

WORKDIR /app
COPY . .

# ==============================================================================
# ШАГ 1: Проверка покрытия логики FSM_parser.cpp (Host Unit Tests >= 80%)
# ==============================================================================
# Компилируем хостовый таргет под x86 GCC. Если покрытие ниже 80%, gcovr вернет exit code 2
# и заблокирует дальнейшее выполнение Docker build.
RUN cmake -B build_host -DENABLE_COVERAGE=ON && \
    cmake --build build_host && \
    ./build_host/fsm_unit_tests && \
    gcovr -r . --filter "Core/Src/protocol/src/FSM_parser.cpp" --fail-under-line 80

# ==============================================================================
# ШАГ 2: Сборка целевой прошивки под STM32F411 (ARM Cortex-M4)
# ==============================================================================
RUN cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-DCI_RENODE_TEST" -DCMAKE_C_FLAGS="-DCI_RENODE_TEST" && \
    cmake --build build

# Проверка физических лимитов Flash и RAM памяти
RUN python3 tests/check_size.py

# ==============================================================================
# ШАГ 3: Интеграционные тесты эмулятора Renode + Python
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
COPY --from=builder /app/build/UART_DRIVER.elf /
