import argparse
import serial
import struct
import sys
import time

# Константы протокола
PREAMBLE = b"\xAA\x55"
CURRENT_VERSION = 0x01
MAX_ALLOWED_PAYLOAD_SIZE = 512

class MsgType:
    DATA = 0x01
    ACK = 0x02
    NACK = 0x03
    COMMAND = 0x10
    STATS_RESP = 0x11

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

def build_frame(version: int, msg_type: int, seq_num: int, payload: bytes, corrupt_crc: bool = False) -> bytes:
    payload_len = len(payload)
    header = struct.pack(">BBBH", version, msg_type, seq_num, payload_len)
    crc_data = header + payload
    crc = calculate_crc16(crc_data)

    if corrupt_crc:
        crc ^= 0xFFFF

    return PREAMBLE + header + payload + struct.pack(">H", crc)

def count_responses(data: bytes, expected_type: int) -> int:
    data = data.replace(b'\xFF\xFF', b'\xFF')
    count = 0
    idx = 0

    while idx < len(data):
        pos = data.find(PREAMBLE, idx)
        if pos == -1:
            break

        if pos + 6 < len(data):
            msg_type = data[pos + 3]
            if msg_type == expected_type:
                count += 1

        # ВАЖНО: Всегда сдвигаемся только на 2 байта (размер преамбулы)!
        # Это гарантирует, что мы не перепрыгнем слипшийся следующий пакет.
        idx = pos + 2

    return count

def verify_mcu_readiness(ser: serial.Serial, timeout: float = 0.5, retries: int = 5) -> bool:
    ping_frame = build_frame(CURRENT_VERSION, MsgType.COMMAND, seq_num=0xFE, payload=b"PING")

    for _ in range(retries):
        ser.reset_input_buffer()
        ser.write(ping_frame.replace(b'\xFF', b'\xFF\xFF'))

        start_time = time.time()
        rx_buffer = b""

        while time.time() - start_time < timeout:
            if ser.in_waiting > 0:
                rx_buffer += ser.read(ser.in_waiting)
            time.sleep(0.01)

        if count_responses(rx_buffer, MsgType.STATS_RESP) > 0:
            return True

    return False

def generate_test_cases():
    test_cases = []

    test_cases.append({
        "name": "1. Standard frame with empty payload",
        "data": build_frame(CURRENT_VERSION, MsgType.COMMAND, seq_num=0, payload=b""),
        "expected": "SUCCESS",
        "expected_type": MsgType.STATS_RESP
    })

    test_cases.append({
        "name": "2. Standard frame with regular payload",
        "data": build_frame(CURRENT_VERSION, MsgType.DATA, seq_num=1, payload=b"Hello STM32"),
        "expected": "SUCCESS",
        "expected_type": MsgType.ACK
    })

    test_cases.append({
        "name": "3. Boundary: Max payload size (512 bytes)",
        "data": build_frame(CURRENT_VERSION, MsgType.DATA, seq_num=2, payload=b"\x55" * MAX_ALLOWED_PAYLOAD_SIZE),
        "expected": "SUCCESS",
        "expected_type": MsgType.ACK
    })

    test_cases.append({
        "name": "4. Boundary: Payload too large (513 bytes)",
        "data": build_frame(CURRENT_VERSION, MsgType.DATA, seq_num=3, payload=b"\xAA" * (MAX_ALLOWED_PAYLOAD_SIZE + 1)),
        "expected": "SILENCE",
        "expected_type": MsgType.ACK
    })

    test_cases.append({
        "name": "5. Boundary: Preamble inside payload",
        "data": build_frame(CURRENT_VERSION, MsgType.COMMAND, seq_num=4, payload=b"Some data \xAA\x55 hidden inside"),
        "expected": "SUCCESS",
        "expected_type": MsgType.STATS_RESP
    })

    test_cases.append({
        "name": "6. Error: Corrupted CRC",
        "data": build_frame(CURRENT_VERSION, MsgType.COMMAND, seq_num=5, payload=b"Test CRC", corrupt_crc=True),
        "expected": "SILENCE",
        "expected_type": MsgType.STATS_RESP
    })

    test_cases.append({
        "name": "7. Error: Bad protocol version",
        "data": build_frame(0x02, MsgType.COMMAND, seq_num=6, payload=b"Bad Version"),
        "expected": "SILENCE",
        "expected_type": MsgType.STATS_RESP
    })

    full_frame = build_frame(CURRENT_VERSION, MsgType.DATA, seq_num=7, payload=b"Cut me please")
    test_cases.append({
        "name": "8. Incomplete frame (Cut in the middle)",
        "data": full_frame[: len(full_frame) - 3],
        "expected": "SILENCE",
        "expected_type": MsgType.ACK
    })

    garbage_prefix = b"\x00\xFF\x12\x34" + build_frame(CURRENT_VERSION, MsgType.COMMAND, seq_num=8, payload=b"After garbage")
    test_cases.append({
        "name": "9. Resync test: Garbage before preamble",
        "data": garbage_prefix,
        "expected": "SUCCESS",
        "expected_type": MsgType.STATS_RESP
    })

    test_cases.append({
        "name": "10. Sequence number wrap-around (seq = 0)",
        "data": build_frame(CURRENT_VERSION, MsgType.DATA, seq_num=0, payload=b"Wrap"),
        "expected": "SUCCESS",
        "expected_type": MsgType.ACK
    })

    test_cases.append({
        "name": "11. Maximum sequence number (seq = 255)",
        "data": build_frame(CURRENT_VERSION, MsgType.DATA, seq_num=255, payload=b"Max Seq"),
        "expected": "SUCCESS",
        "expected_type": MsgType.ACK
    })

    frame1 = build_frame(CURRENT_VERSION, MsgType.COMMAND, seq_num=10, payload=b"First")
    frame2 = build_frame(CURRENT_VERSION, MsgType.COMMAND, seq_num=11, payload=b"Second")
    test_cases.append({
        "name": "12. Concatenated back-to-back frames",
        "data": frame1 + frame2,
        "expected": "SUCCESS_DOUBLE",
        "expected_type": MsgType.STATS_RESP
    })

    return test_cases

def run_single_test(ser: serial.Serial, test: dict, timeout: float = 0.5) -> bool:
    ser.reset_input_buffer()

    raw_data = test["data"].replace(b'\xFF', b'\xFF\xFF')
    ser.write(raw_data)

    start_time = time.time()
    rx_buffer = b""

    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            rx_buffer += ser.read(ser.in_waiting)
        time.sleep(0.01)

    expected_type = test["expected_type"]
    resp_count = count_responses(rx_buffer, expected_type)
    expected = test["expected"]

    if expected == "SUCCESS":
        if resp_count >= 1:
            print(f"    ✅ Passed: Корректный ответ (0x{expected_type:02X}) получен")
            return True
        print(f"    ❌ Failed: Ответ (0x{expected_type:02X}) не получен (таймаут)")
        return False

    elif expected == "SUCCESS_DOUBLE":
        if resp_count == 2:
            print(f"    ✅ Passed: Получено 2 пакета (0x{expected_type:02X})")
            return True
        print(f"    ❌ Failed: Ожидалось 2 пакета, получено {resp_count}")
        return False

    elif expected == "SILENCE":
        if resp_count == 0:
            print("    ✅ Passed: Некорректный пакет штатно проигнорирован")
            return True
        print("    ❌ Failed: Получен ответ на заведомо ошибочный пакет!")
        return False

    return False

def main():
    parser = argparse.ArgumentParser(description="Renode Integration Test Runner")
    parser.add_argument("--url", default="socket://localhost:4321")
    parser.add_argument("--timeout", type=float, default=0.5)
    parser.add_argument("--retries", type=int, default=10)
    args = parser.parse_args()

    print("=== ЗАПУСК ИНТЕГРАЦИОННЫХ ТЕСТОВ В RENODE ===")
    print(f"Подключение: {args.url}\n")

    ser = None
    connected_and_ready = False

    for attempt in range(1, args.retries + 1):
        try:
            ser = serial.serial_for_url(args.url, timeout=0.1)
            if verify_mcu_readiness(ser, timeout=args.timeout):
                connected_and_ready = True
                print("✅ Соединение с Renode и прошивкой MCU успешно установлено!\n")
                break
            else:
                ser.close()
                print(f"  [Попытка {attempt}/{args.retries}] Сокет открыт, но MCU не отвечает на PING...")
        except serial.SerialException:
            print(f"  [Попытка {attempt}/{args.retries}] Ожидание открытия сокета Renode...")
        time.sleep(0.5)

    if not connected_and_ready or ser is None:
        print(f"\n❌ Ошибка: Не удалось установить связь с прошивкой в Renode ({args.url})")
        sys.exit(1)

    test_cases = generate_test_cases()
    failed_count = 0

    with ser:
        ser.reset_input_buffer()
        for idx, tc in enumerate(test_cases, 1):
            print(f"[TEST {idx}/{len(test_cases)}] {tc['name']}")
            if not run_single_test(ser, tc, timeout=args.timeout):
                failed_count += 1
            time.sleep(0.05)

    print("=" * 60)
    if failed_count == 0:
        print(f"🎉 УСПЕХ: Все {len(test_cases)} тестов успешно пройдены!")
        sys.exit(0)
    else:
        print(f"💥 ОШИБКА: Провалено тестов: {failed_count} из {len(test_cases)}")
        sys.exit(1)

if __name__ == "__main__":
    main()
