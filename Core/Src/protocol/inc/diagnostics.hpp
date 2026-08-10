#pragma once

#include <cstdint>
#include <cstddef>

namespace protocol {

// Чистая структура данных (без методов логики и reset)
struct DiagnosticsStats {
    // 1. Статистика пакетов протокола
    uint32_t rx_frames_ok{0};        // Успешно принятые и распарсенные кадры
    uint32_t tx_frames_ok{0};        // Успешно отправленные кадры
    
    // 2. Ошибки протокола и парсера
    uint32_t crc_errors{0};          // Ошибки контрольной суммы CRC16
    uint32_t length_errors{0};       // Превышение допустимой длины payload
    uint32_t timeout_errors{0};      // Потеря пакетов / истечение таймаута ACK
    uint32_t resync_events{0};       // Сбросы FSM (потеря преамбулы, битые данные)
    
    // 3. Буферы и шина
    uint32_t rx_buffer_overflows{0}; // Переполнение кольцевого буфера приёма
    uint32_t tx_buffer_overflows{0}; // Переполнение буфера передачи
    
    // 4. Аппаратные ошибки UART (считываются из регистров STM32)
    uint32_t hw_framing_errors{0};   // Ошибка кадра (Framing Error)
    uint32_t hw_parity_errors{0};    // Ошибка чётности (Parity Error)
    uint32_t hw_overrun_errors{0};   // Аппаратное переполнение UART (Overrun Error)
};

// Класс-сервис: отвечает за логику сброса, управления и форматирования вывода
class DiagnosticsService {
public:
    explicit DiagnosticsService(DiagnosticsStats& stats) : stats_(stats) {}

    // Сброс статистики (теперь логика сброса живет здесь)
    void clear();

    // Доступ к счетчикам
    DiagnosticsStats& get_stats() { return stats_; }
    const DiagnosticsStats& get_stats() const { return stats_; }

    // Сформировать текстовый CLI-отчет в передаваемый буфер
    size_t print_cli_report(char* buffer, size_t max_len) const;

private:
    DiagnosticsStats& stats_;
};

} 