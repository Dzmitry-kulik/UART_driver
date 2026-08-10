#pragma once

#include "frame.hpp"
#include "diagnostics.hpp"
#include <cstdint>
#include <cstddef>
#include <functional>

namespace protocol {

using TxBytesCallback = std::function<bool(const uint8_t* data, size_t size)>;

class ReceiverTransport {
public:
    ReceiverTransport(DiagnosticsStats& stats, TxBytesCallback tx_cb);

    // Вызывается из парсера, когда принят валидный кадр.
    // Возвращает true, если кадр НОВЫЙ и его нужно передать в бизнес-логику.
    bool process_incoming_frame(const Frame& frame);

private:
    DiagnosticsStats& stats_;
    TxBytesCallback   tx_cb_;

    // Состояние приёмника: запоминаем seq_num последнего успешно выполненного кадра
    uint8_t  last_rx_seq_num_{0xFF};
    bool     has_rx_seq_{false};

    void send_ack(uint8_t seq_num);
    void send_nack(uint8_t seq_num);
    bool transmit_frame(const Frame& frame);
};

} 