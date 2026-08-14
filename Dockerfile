FROM ghcr.io/dzmitry-kulik/stm32-ci-base:latest AS builder

WORKDIR /app
COPY . .

# 1. Host Unit-тесты (Ninja) - ЗАКОММЕНТИРОВАНО
# RUN cmake -B build_host -G Ninja -DENABLE_COVERAGE=ON && \
#     ninja -C build_host -j$(nproc) && \
#     ./build_host/fsm_unit_tests && \
#     gcovr --gcov-executable gcov-12 -r . --filter "Core/Src/protocol/src/FSM_parser.cpp" --fail-under-line 80

# 2. Сборка ТЕСТОВОЙ прошивки (для Renode / эмулятора)
RUN cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-DCI_RENODE_TEST" -DCMAKE_C_FLAGS="-DCI_RENODE_TEST" && \
    ninja -C build -j$(nproc)

# 3. Интеграционный прогон в Renode - ЗАКОММЕНТИРОВАНО
# RUN renode --disable-xwt tests/test_board.resc & PID=$! && \
#     sleep 3 && \
#     python3 tests/tests_renode.py --url socket://localhost:4321 --timeout 0.5 && \
#     python3 tests/benchmark.py --url socket://localhost:4321 --output build/benchmark_results.png && \
#     python3 tests/stress_test.py --url socket://localhost:4321 --duration 10; \
#     TEST_RESULT=$? ; \
#     gdb-multiarch build/UART_DRIVER.elf -batch -x tests/ci_debug.gdb ; \
#     kill $PID ; \
#     exit $TEST_RESULT

# 4. Сборка БОЕВОЙ прошивки для железа (С отладочными символами)
RUN cmake -B build_prod -G Ninja -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo && \
    ninja -C build_prod -j$(nproc)

# Этап артефактов
FROM scratch AS artifacts
COPY --from=builder /app/build_prod/UART_DRIVER.elf /
# Закомментировано, так как график бенчмарка больше не генерируется без тестов:
# COPY --from=builder /app/build/benchmark_results.png /
