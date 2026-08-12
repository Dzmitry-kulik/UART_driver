import subprocess
import sys
import os

def check_firmware_size(elf_path, max_flash_kb=512):
    if not os.path.exists(elf_path):
        print(f"❌ Ошибка: Файл прошивки не найден: {elf_path}")
        sys.exit(1)

    # Запускаем arm-none-eabi-size
    result = subprocess.run(["arm-none-eabi-size", elf_path], capture_output=True, text=True)
    if result.returncode != 0:
        print(f"❌ Ошибка при вызове размера: {result.stderr}")
        sys.exit(1)

    print(result.stdout)

    # Парсим вывод утилиты (вторая строка содержит десятичные байты text, data, bss)
    lines = result.stdout.strip().split("\n")
    if len(lines) < 2:
        print("❌ Некорректный вывод утилиты size")
        sys.exit(1)

    parts = lines[1].split()
    text_size = int(parts[0])
    data_size = int(parts[1])

    total_flash_used = text_size + data_size
    max_bytes = max_flash_kb * 1024

    print(f"📊 Использовано Flash: {total_flash_used} байт из {max_bytes} байт ({total_flash_used / max_bytes * 100:.2f}%)")

    if total_flash_used > max_bytes:
        print(f"❌ Ошибка: Прошивка превышает допустимый размер Flash ({max_flash_kb} КБ)!")
        sys.exit(1)
    else:
        print("✅ Проверка размера Flash успешно пройдена.")

if __name__ == "__main__":
    # Убрали расширение .elf, теперь скрипт ищет правильный файл
    elf_file = "build/UART_DRIVER.elf"
    check_firmware_size(elf_file)
