import argparse
import time
import struct
import serial
import sys
import matplotlib.pyplot as plt

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

def wait_for_ack(ser: serial.Serial, expected_seq: int, timeout: float = 1.0) -> bool:
    """Ожидает получение ACK-кадра с совпадением sequence number."""
    start = time.time()
    rx_buf = b""
    while time.time() - start < timeout:
        if ser.in_waiting > 0:
            rx_buf += ser.read(ser.in_waiting).replace(b"\xFF\xFF", b"\xFF")
            idx = 0
            while idx < len(rx_buf):
                pos = rx_buf.find(PREAMBLE, idx)
                if pos == -1:
                    break
                # Кадр ACK имеет длину 9 байт
                if pos + 9 <= len(rx_buf):
                    # Преамбула + Версия + ACK + SEQ
                    if rx_buf[pos+3] == MSG_ACK and rx_buf[pos+4] == expected_seq:
                        return True
                    idx = pos + 9
                else:
                    break
        time.sleep(0.001)
    return False

def run_benchmark(ser: serial.Serial, payload_sizes: list[int], iterations: int = 100):
    payload_results = []
    rtt_results = []
    throughput_results = []

    print(f"🚀 Запуск бенчмарка ({iterations} итераций на каждый размер кадра)...\n")
    print(f"{'Payload (байты)':<18} | {'Latency RTT (мс)':<18} | {'Throughput (КБ/с)':<18}")
    print("-" * 60)

    seq_num = 0

    for size in payload_sizes:
        payload = b"X" * size
        frame = build_frame(seq_num, payload)
        raw_frame = frame.replace(b"\xFF", b"\xFF\xFF") # Экранирование для Telnet/Renode
        total_frame_len = len(frame) # Заголовок (7B) + Payload + CRC (2B)

        total_rtt = 0.0
        success_cnt = 0

        for _ in range(iterations):
            ser.reset_input_buffer()

            t_start = time.perf_counter()
            ser.write(raw_frame)

            if wait_for_ack(ser, seq_num, timeout=0.5):
                t_end = time.perf_counter()
                total_rtt += (t_end - t_start)
                success_cnt += 1

            seq_num = (seq_num + 1) % 256

        if success_cnt == 0:
            print(f"{size:<18} | FAILED (Нет ответов)")
            continue

        avg_rtt_ms = (total_rtt / success_cnt) * 1000.0

        # Пропускная способность = (Размер полезных данных + заголовок ACK и DATA) / RTT
        # Кадр DATA (7B + size + 2B) + Кадр ACK (9B)
        total_bytes_transferred = total_frame_len + 9
        avg_throughput_kbps = (total_bytes_transferred / (avg_rtt_ms / 1000.0)) / 1024.0

        payload_results.append(size)
        rtt_results.append(avg_rtt_ms)
        throughput_results.append(avg_throughput_kbps)

        print(f"{size:<18} | {avg_rtt_ms:<18.3f} | {avg_throughput_kbps:<18.2f}")

    return payload_results, rtt_results, throughput_results

def plot_metrics(sizes, rtts, throughputs, output_file="benchmark_results.png"):
    """Создает два графика (Latency & Throughput) и сохраняет картинку."""
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), sharex=True)

    # График 1: Задержка (Latency / RTT)
    ax1.plot(sizes, rtts, 'o-', color='crimson', linewidth=2, markersize=6)
    ax1.set_ylabel("Round-Trip Time (мс)", fontsize=11)
    ax1.set_title("Зависимость задержки (RTT) от размера полезных данных (Payload)", fontsize=12)
    ax1.grid(True, linestyle="--", alpha=0.7)

    # График 2: Пропускная способность (Throughput)
    ax2.plot(sizes, throughputs, 's-', color='teal', linewidth=2, markersize=6)
    ax2.set_xlabel("Размер полезной нагрузки / Payload (байты)", fontsize=11)
    ax2.set_ylabel("Пропускная способность (КБ/с)", fontsize=11)
    ax2.set_title("Реальная пропускная способность канала (Throughput)", fontsize=12)
    ax2.grid(True, linestyle="--", alpha=0.7)

    plt.tight_layout()
    plt.savefig(output_file, dpi=300)
    print(f"\n📊 График сохранен в файл: {output_file}")

def main():
    parser = argparse.ArgumentParser(description="MCU Protocol Benchmark")
    parser.add_argument("--url", default="socket://localhost:4321", help="Serial socket URL")
    parser.add_argument("--output", default="benchmark_results.png", help="Output file for the generated chart")
    args = parser.parse_args()

    # Размеры пакетов для тестирования (от пустых до максимальных 512B)
    test_sizes = [0, 16, 32, 64, 128, 256, 384, 512]

    try:
        ser = serial.serial_for_url(args.url, timeout=0.1)
    except Exception as e:
        print(f"❌ Ошибка подключения к {args.url}: {e}")
        sys.exit(1)

    with ser:
        # Для CI достаточно 50 итераций на размер, чтобы не затягивать сборку,
        # но получить статистически значимое усреднение.
        sizes, rtts, throughputs = run_benchmark(ser, test_sizes, iterations=50)

        if sizes:
            plot_metrics(sizes, rtts, throughputs, args.output)
        else:
            print("❌ Бенчмарк провален, нет данных для построения графика.")
            sys.exit(1)

if __name__ == "__main__":
    main()
