import argparse
import serial
import struct
import sys
import time
import subprocess
import re

PREAMBLE = b"\xAA\x55"
CURRENT_VERSION = 0x01
MSG_DATA = 0x01

def calculate_crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

def build_frame(seq_num: int, payload: bytes) -> bytes:
    header = struct.pack(">BBBH", CURRENT_VERSION, MSG_DATA, seq_num, len(payload))
    crc = calculate_crc16(header + payload)
    return PREAMBLE + header + payload + struct.pack(">H", crc)

def read_mcu_stats(elf_path: str) -> dict:
    """Подключается к Renode через GDB и считывает текущую структуру g_stats."""
    cmd = [
        "gdb-multiarch", elf_path, "-batch",
        "-ex", "target remote localhost:3333",
        "-ex", "print g_stats"
    ]
    try:
        out = subprocess.check_output(cmd, stderr=subprocess.STDOUT, text=True)
        stats = {}
        for match in re.finditer(r"(\w+)\s*=\s*(\d+)", out):
            stats[match.group(1)] = int(match.group(2))
        return stats
    except Exception as e:
        print(f"⚠️ Ошибка чтения g_stats через GDB: {e}")
        return {}

def run_stress_test(ser: serial.Serial, duration_sec: int) -> int:
    print(f"🚀 Запуск стресс-теста ({duration_sec} сек) на скорости ~115200 baud...")

    payload = b"STRESS_TEST_PACKET_64B_PAYLOAD_FOR_STM32_RING_BUFFER_TESTING__"
    seq_num = 0
    sent_frames = 0

    start_time = time.time()
    last_report = start_time

    BAUD_RATE = 115200
    BYTES_PER_SEC = BAUD_RATE / 10.0

    while time.time() - start_time < duration_sec:
        frame = build_frame(seq_num, payload)
        raw_frame = frame.replace(b"\xFF", b"\xFF\xFF")
        seq_num = (seq_num + 1) % 256
        sent_frames += 1

        t_tx_start = time.perf_counter()
        ser.write(raw_frame)

        # === ИСПРАВЛЕНИЕ ЗДЕСЬ ===
        # Заворачиваем чтение в try-except для защиты от сбоев сокета при высокой нагрузке
        try:
            if ser.in_waiting > 0:
                ser.read(ser.in_waiting)
        except OSError:
            pass

        # Дросселирование под 115200 baud
        tx_time_seconds = len(raw_frame) / BYTES_PER_SEC
        elapsed = time.perf_counter() - t_tx_start
        if elapsed < tx_time_seconds:
            time.sleep(tx_time_seconds - elapsed)

        current_time = time.time()
        if current_time - last_report >= 30:
            print(f"  ⏱️ [{int(current_time - start_time)}/{duration_sec} сек] Отправлено кадров с ПК: {sent_frames}")
            last_report = current_time

    # Принудительно проталкиваем остатки байт из сетевого буфера
    try:
        ser.flush()
    except Exception:
        pass

    # Выдерживаем увеличенную паузу для гарантированной вычитки буфера микроконтроллером
    print("⏳ Завершение потока и выдержка паузы (3.5 сек) для обработки последних байт...")
    time.sleep(3.5)

    # === ИСПРАВЛЕНИЕ ЗДЕСЬ ===
    try:
        if ser.in_waiting > 0:
            ser.read(ser.in_waiting)
    except OSError:
        pass

    return sent_frames

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="socket://localhost:4321")
    parser.add_argument("--duration", type=int, default=300)
    parser.add_argument("--elf", default="build/UART_DRIVER.elf", help="Path to firmware ELF")
    args = parser.parse_args()

    # 1. Замер начальных счётчиков MCU перед тестом
    initial_stats = read_mcu_stats(args.elf)
    initial_rx_ok = initial_stats.get("rx_frames_ok", 0)

    # 2. Прогон непрерывного потока
    try:
        ser = serial.serial_for_url(args.url, timeout=0.1)
    except serial.SerialException as e:
        print(f"❌ Не удалось открыть сокет {args.url}: {e}")
        sys.exit(1)

    with ser:
        sent_frames = run_stress_test(ser, args.duration)

    # 3. Финальный сброс и проверка внутренних счётчиков MCU через GDB
    final_stats = read_mcu_stats(args.elf)
    final_rx_ok = final_stats.get("rx_frames_ok", 0)
    mcu_received_frames = final_rx_ok - initial_rx_ok

    overflows = final_stats.get("rx_buffer_overflows", 0)
    hw_overruns = final_stats.get("hw_overrun_errors", 0)
    crc_errs = final_stats.get("crc_errors", 0) - initial_stats.get("crc_errors", 0)

    print("\n" + "=" * 60)
    print("📊 ИТОГИ СТРЕСС-ТЕСТА (ПО СЧЁТЧИКАМ MCU):")
    print(f"  • Отправлено кадров с ПК:          {sent_frames}")
    print(f"  • Принято и распознано MCU:        {mcu_received_frames}")
    print(f"  • Программных переполнений (SW):   {overflows}")
    print(f"  • Аппаратных переполнений (HW):    {hw_overruns}")
    print(f"  • Ошибок CRC за время теста:       {crc_errs}")
    print("=" * 60)

    # Проверка условий сдачи ТЗ
    failed = False
    if mcu_received_frames < sent_frames:
        print(f"❌ ТЕСТ ПРОВАЛЕН: MCU пропустил {sent_frames - mcu_received_frames} кадров!")
        failed = True

    if overflows > 0:
        print(f"❌ ТЕСТ ПРОВАЛЕН: Обнаружено {overflows} программных переполнений кольцевого буфера!")
        failed = True

    if hw_overruns > 0:
        print(f"❌ ТЕСТ ПРОВАЛЕН: Обнаружено {hw_overruns} аппаратных переполнений UART (ORE)!")
        failed = True

    if failed:
        sys.exit(1)

    print("🎉 ТЕСТ УСПЕШНО ПРОЙДЕН: 0% потерь и 0 переполнений на стороне MCU!")
    sys.exit(0)

if __name__ == "__main__":
    main()
