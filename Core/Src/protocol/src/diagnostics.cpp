#include "diagnostics.hpp"
#include <cstdio>

namespace protocol {

void DiagnosticsService::clear() {
    stats_ = DiagnosticsStats{};
}

size_t DiagnosticsService::print_cli_report(char* buffer, size_t max_len) const {
    if (!buffer || max_len == 0) {
        return 0;
    }

    int written = std::snprintf(
        buffer, max_len,
        "\r\n=== UART DRIVER DIAGNOSTICS ===\r\n"
        "[ OK ] RX Frames:       %lu\r\n"
        "[ OK ] TX Frames:       %lu\r\n"
        "-------------------------------\r\n"
        "[ERR] CRC Mismatches:  %lu\r\n"
        "[ERR] Invalid Length:  %lu\r\n"
        "[ERR] ACK Timeouts:    %lu\r\n"
        "[ERR] FSM Resyncs:     %lu\r\n"
        "-------------------------------\r\n"
        "[BUF] RX Overflows:    %lu\r\n"
        "[BUF] TX Overflows:    %lu\r\n"
        "-------------------------------\r\n"
        "[ HW] Framing Errors:  %lu\r\n"
        "[ HW] Parity Errors:   %lu\r\n"
        "[ HW] Overrun Errors:  %lu\r\n"
        "===============================\r\n",
        static_cast<unsigned long>(stats_.rx_frames_ok),
        static_cast<unsigned long>(stats_.tx_frames_ok),
        static_cast<unsigned long>(stats_.crc_errors),
        static_cast<unsigned long>(stats_.length_errors),
        static_cast<unsigned long>(stats_.timeout_errors),
        static_cast<unsigned long>(stats_.resync_events),
        static_cast<unsigned long>(stats_.rx_buffer_overflows),
        static_cast<unsigned long>(stats_.tx_buffer_overflows),
        static_cast<unsigned long>(stats_.hw_framing_errors),
        static_cast<unsigned long>(stats_.hw_parity_errors),
        static_cast<unsigned long>(stats_.hw_overrun_errors)
    );

    if (written < 0) {
        return 0;
    }

    return (static_cast<size_t>(written) < max_len) ? static_cast<size_t>(written) : (max_len - 1);
}

} 