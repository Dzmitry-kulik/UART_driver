import struct
import serial
import time
import sys
import argparse
PREAMBLE = b"\xAA\x55"
CURRENT_VERSION = 0x01
MAX_ALLOWED_PAYLOAD_SIZE = 512


class MsgType:
    DATA = 0x01
    ACK = 0x02
    NACK = 0x03
    COMMAND = 0x10


def calculate_crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= (byte << 8)
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
        crc ^= 0xFFFF  # Портим CRC

    return PREAMBLE + header + payload + struct.pack(">H", crc)


def generate_test_cases():
    """Формирование 12 тестовых наборов."""
    test_cases = []

    # 1. Штатный кадр (пустой payload)
    test_cases.append({
        "name": "1. Standard frame with empty payload",
        "data": build_frame(CURRENT_VERSION, MsgType.COMMAND, seq_num=0, payload=b""),
        "expected": "SUCCESS"
    })

    # 2. Штатный кадр с данными
    test_cases.append({
        "name": "2. Standard frame with regular payload",
        "data": build_frame(CURRENT_VERSION, MsgType.DATA, seq_num=1, payload=b"Hello STM32"),
        "expected": "SUCCESS"
    })

    # 3. Граница: Максимально допустимая длина payload (512 байт)
    max_payload = b"\x55" * MAX_ALLOWED_PAYLOAD_SIZE
    test_cases.append({
        "name": "3. Boundary: Max payload size (512 bytes)",
        "data": build_frame(CURRENT_VERSION, MsgType.DATA, seq_num=2, payload=max_payload),
        "expected": "SUCCESS"
    })

    # 4. Граница: Превышение длины payload (513 байт)
    oversized_payload = b"\xAA" * (MAX_ALLOWED_PAYLOAD_SIZE + 1)
    test_cases.append({
        "name": "4. Boundary: Payload too large (513 bytes)",
        "data": build_frame(CURRENT_VERSION, MsgType.DATA, seq_num=3, payload=oversized_payload),
        "expected": "SILENCE"
    })

    # 5. Граница: Преамбула внутри payload
    payload_with_preamble = b"Some data \xAA\x55 hidden inside"
    test_cases.append({
        "name": "5. Boundary: Preamble inside payload",
        "data": build_frame(CURRENT_VERSION, MsgType.COMMAND, seq_num=4, payload=payload_with_preamble),
        "expected": "SUCCESS"
    })

    # 6. Ошибка: Неверная контрольная сумма
    test_cases.append({
        "name": "6. Error: Corrupted CRC",
        "data": build_frame(CURRENT_VERSION, MsgType.COMMAND, seq_num=5, payload=b"Test CRC", corrupt_crc=True),
        "expected": "SILENCE"
    })

    # 7. Ошибка: Неподдерживаемая версия
    test_cases.append({
        "name": "7. Error: Bad protocol version",
        "data": build_frame(0x02, MsgType.COMMAND, seq_num=6, payload=b"Bad Version"),
        "expected": "SILENCE"
    })

    # 8. Обрыв кадра
    full_frame = build_frame(CURRENT_VERSION, MsgType.DATA, seq_num=7, payload=b"Cut me please")
    test_cases.append({
        "name": "8. Incomplete frame (Cut in the middle)",
        "data": full_frame[:len(full_frame) - 3],
        "expected": "SILENCE"
    })

    # 9. Мусор перед преамбулой (Ресинхронизация)
    garbage_prefix = b"\x00\xFF\x12\x34" + build_frame(CURRENT_VERSION, MsgType.COMMAND, seq_num=8, payload=b"After garbage")
    test_cases.append({
        "name": "9. Resync test: Garbage before preamble",
        "data": garbage_prefix,
        "expected": "SUCCESS"
    })

    # 10. Переполнение seq_num (0x00)
    test_cases.append({
        "name": "10. Sequence number wrap-around (seq = 0)",
        "data": build_frame(CURRENT_VERSION, MsgType.ACK, seq_num=0, payload=b"Wrap"),
        "expected": "SUCCESS"
    })

    # 11. Максимальный seq_num (0xFF)
    test_cases.append({
        "name": "11. Maximum sequence number (seq = 255)",
        "data": build_frame(CURRENT_VERSION, MsgType.ACK, seq_num=255, payload=b"Max Seq"),
        "expected": "SUCCESS"
    })

    # 12. Поток из двух склеенных кадров
    frame1 = build_frame(CURRENT_VERSION, MsgType.COMMAND, seq_num=10, payload=b"First")
    frame2 = build_frame(CURRENT_VERSION, MsgType.COMMAND, seq_num=11, payload=b"Second")
    test_cases.append({
        "name": "12. Concatenated back-to-back frames",
        "data": frame1 + frame2,
        "expected": "SUCCESS_DOUBLE"
    })

    return test_cases


def count_ack_responses(data: bytes) -> int:
    """Подсчитывает количество правильных ACK кадров в ответе."""
    count = 0
    idx = 0
    while idx < len(data):
        pos = data.find(PREAMBLE, idx)
        if pos == -1:
            break
        # Проверяем байт типа сообщения (смещение 3: 2 байта преамбулы + 1 байт версии)
        if pos + 3 < len(data) and data[pos + 3] == MsgType.ACK:
            count += 1
        idx = pos + 1
    return count


def run_single_test(ser: serial.Serial, test: dict, timeout: float = 0.3) -> bool:
    """Выполняет один тест и сверяет результат."""
    ser.reset_input_buffer()
    ser.write(test["data"])

    start_time = time.time()
    rx_buffer = b""

    # Накапливаем ответ за отведённый таймаут
    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            rx_buffer += ser.read(ser.in_waiting)
        time.sleep(0.01)

    ack_count = count_ack_responses(rx_buffer)
    expected = test["expected"]

    if expected == "SUCCESS":
        if ack_count >= 1:
            print("    ✅ Passed: ACK получен")
            return True
        print("    ❌ Failed: ACK не получен (таймаут)")
        return False

    elif expected == "SUCCESS_DOUBLE":
        if ack_count == 2:
            print("    ✅ Passed: Получено 2 ACK кадра")
            return True
        print(f"    ❌ Failed: Ожидалось 2 ACK, получено {ack_count}")
        return False

    elif expected == "SILENCE":
        if ack_count == 0:
            print("    ✅ Passed: Некорректный пакет проигнорирован (ACK отсутствует)")
            return True
        print("    ❌ Failed: Получен ACK на ошибочный пакет!")
        return False

    return False


def main():
    parser = argparse.ArgumentParser(description="Renode Integration Test Runner")
    parser.add_argument("--url", default="socket://localhost:1234", help="Serial/Socket connection URL")
    args = parser.parse_args()

    print(f"=== ЗАПУСК ИНТЕГРАЦИОННЫХ ТЕСТОВ В RENODE ===")
    print(f"Подключение: {args.url}\n")

    # Попытки подключения (ждём пока Renode поднимет сокет)
    ser = None
    for attempt in range(1, 11):
        try:
            ser = serial.serial_for_url(args.url, timeout=0.1)
            print("✅ Соединение с Renode успешно установлено!\n")
            break
        except serial.SerialException:
            time.sleep(0.5)

    if not ser:
        print(f"❌ Ошибка: Не удалось подключиться к эмулятору Renode ({args.url})")
        sys.exit(1)

    test_cases = generate_test_cases()
    failed_count = 0

    with ser:
        for idx, tc in enumerate(test_cases, 1):
            print(f"[TEST {idx}/{len(test_cases)}] {tc['name']}")
            if not run_single_test(ser, tc):
                failed_count += 1
            time.sleep(0.02)

    print("=" * 60)
    if failed_count == 0:
        print(f"🎉 УСПЕХ: Все {len(test_cases)} тестов успешно пройдены!")
        sys.exit(0)
    else:
        print(f"💥 ОШИБКА: Провалено тестов: {failed_count} из {len(test_cases)}")
        sys.exit(1)


if __name__ == "__main__":
    main()
