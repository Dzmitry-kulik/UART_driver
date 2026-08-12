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
MSG_ACK = 0x02

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

def count_ack_responses(data: bytes) -> int:
    """Ищет ACK-кадры в сыром потоке."""
    count = 0
    idx = 0
    while idx < len(data):
        pos = data.find(PREAMBLE, idx)
        if pos == -1:
            break
        # Проверяем, что это пакет нашей версии и типа ACK
        if pos + 3 < len(data) and data[pos + 3] == MSG_ACK:
            count += 1
            idx = pos + 9 # Пропускаем весь кадр ACK
        else:
            idx = pos + 1
    return count

def verify_mcu_counters(elf_path: str) -> bool:
    """Подключается к Renode через GDB, читает g_stats и проверяет переполнения."""
    print("\n🔍 Чтение счётчиков g_stats из RAM микроконтроллера через GDB...")
    cmd = [
        "gdb-multiarch", elf_path, "-batch",
        "-ex", "target remote localhost:3333",
        "-ex", "print g_stats"
    ]

    try:
        out = subprocess.check_output(cmd, stderr=subprocess.STDOUT, text=True)
    except subprocess.CalledProcessError as e:
        print(f"❌ Ошибка вызова GDB: {e.output}")
        return False

    # Парсим вывод GDB вида "rx_buffer_overflows = 0"
    stats = {}
    for match in re.finditer(r"(\w+)\s*=\s*(\d+)", out):
        stats[match.group(1)] = int(match.group(2))

    if not stats:
        print("❌ Не удалось распарсить структуру g_stats. Проверьте наличие отладочных символов (-g).")
        return False

    print("📊 Внутренние счётчики MCU:")
    for k, v in stats.items():
        print(f"  • {k}: {v}")

    # Жесткие проверки (Fail conditions)
    failed = False

    if stats.get("rx_buffer_overflows", 0) > 0:
        print("❌ ОШИБКА: Обнаружено программное переполнение кольцевого буфера (rx_buffer_overflows > 0)!")
        failed = True

    if stats.get("hw_overrun_errors", 0) > 0:
        print("❌ ОШИБКА: Обнаружено аппаратное переполнение UART ORE (hw_overrun_errors > 0)!")
        failed = True

    if stats.get("crc_errors", 0) > 0:
        print("⚠️ ПРЕДУПРЕЖДЕНИЕ: Были ошибки CRC. В идеальной среде эмулятора их быть не должно.")

    if failed:
        return False

    print("✅ Потерь и переполнений на стороне MCU не зафиксировано!")
    return True

def run_stress_test(ser: serial.Serial, duration_sec: int) -> bool:
    print(f"🚀 Запуск стресс-теста на {duration_sec} секунд (Максимальная пропускная способность)...")

    payload = b"STRESS_TEST_PACKET_64B_PAYLOAD_FOR_STM32_RING_BUFFER_TESTING__"
    seq_num = 0
    sent_frames = 0
    received_acks = 0

    rx_buffer = b""
    start_time = time.time()
    last_report = start_time

    # Отправляем пачками, чтобы забивать канал, но успевать читать ответы
    frames_per_batch = 10

    while time.time() - start_time < duration_sec:
        batch_data = b""
        for _ in range(frames_per_batch):
            frame = build_frame(seq_num, payload)
            batch_data += frame.replace(b"\xFF", b"\xFF\xFF") # Telnet escape для Renode
            seq_num = (seq_num + 1) % 256
            sent_frames += 1

        ser.write(batch_data)

        if ser.in_waiting > 0:
            rx_buffer += ser.read(ser.in_waiting).replace(b"\xFF\xFF", b"\xFF")
            acks = count_ack_responses(rx_buffer)
            if acks > 0:
                received_acks += acks
                # Сохраняем хвост буфера после последнего найденного ACK
                last_ack_pos = rx_buffer.rfind(PREAMBLE)
                if last_ack_pos != -1:
                    rx_buffer = rx_buffer[last_ack_pos + 9:]

        current_time = time.time()
        if current_time - last_report >= 30:
            print(f"  ⏱️ [{int(current_time - start_time)}/{duration_sec} сек] Отправлено: {sent_frames} | Получено ACK: {received_acks}")
            last_report = current_time

    # Даем 1.5 секунды на обработку последних байт в пайплайне
    print("⏳ Ожидание завершения обработки последних кадров...")
    time.sleep(1.5)
    if ser.in_waiting > 0:
        rx_buffer += ser.read(ser.in_waiting).replace(b"\xFF\xFF", b"\xFF")
        received_acks += count_ack_responses(rx_buffer)

    loss_rate = ((sent_frames - received_acks) / sent_frames) * 100 if sent_frames > 0 else 0

    print("\n" + "=" * 60)
    print("📊 ИТОГИ КАНАЛЬНОГО УРОВНЯ:")
    print(f"  • Отправлено кадров: {sent_frames}")
    print(f"  • Получено ACK:      {received_acks}")
    print(f"  • Процент потерь:    {loss_rate:.3f}%")
    print("=" * 60)

    if sent_frames != received_acks:
        print(f"❌ ТЕСТ ПРОВАЛЕН: Потеряно {sent_frames - received_acks} кадров на уровне канала!")
        return False

    return True

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="socket://localhost:4321")
    parser.add_argument("--duration", type=int, default=300)
    parser.add_argument("--elf", default="build/UART_DRIVER.elf", help="Path to firmware ELF for GDB")
    args = parser.parse_args()

    try:
        ser = serial.serial_for_url(args.url, timeout=0.1)
    except serial.SerialException as e:
        print(f"❌ Не удалось открыть сокет {args.url}: {e}")
        sys.exit(1)

    with ser:
        if not run_stress_test(ser, args.duration):
            sys.exit(1)

    if not verify_mcu_counters(args.elf):
        sys.exit(1)

    sys.exit(0)

if __name__ == "__main__":
    main()
