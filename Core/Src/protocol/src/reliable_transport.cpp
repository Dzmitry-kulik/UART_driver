#include "reliable_transport.hpp"
#include "crc16.hpp"

namespace protocol {

ReceiverTransport::ReceiverTransport(DiagnosticsStats& stats, TxBytesCallback tx_cb)
    : stats_(stats), tx_cb_(tx_cb) {}

bool ReceiverTransport::process_incoming_frame(const Frame& frame) {
    // 1. Проверяем на дубликат (если прошлый ACK потерялся и мастер шлет пакет повторно)
    if (has_rx_seq_ && frame.seq_num == last_rx_seq_num_) {
        send_ack(frame.seq_num); // Повторяем ACK, но в бизнес-логику кадр НЕ отдаем
        return false;
    }

    // 2. Фиксируем новый порядковый номер
    last_rx_seq_num_ = frame.seq_num;
    has_rx_seq_ = true;

    // 3. Отправляем подтверждение об успехе
    send_ack(frame.seq_num);

    return true; // Кадр новый, передаем в AppController!
}

void ReceiverTransport::send_ack(uint8_t seq_num) {
    Frame ack{};
    ack.version = CURRENT_VERSION;
    ack.type = MessageType::ACK;
    ack.seq_num = seq_num;
    transmit_frame(ack);
}

void ReceiverTransport::send_nack(uint8_t seq_num) {
    Frame nack{};
    nack.version = CURRENT_VERSION;
    nack.type = MessageType::NACK;
    nack.seq_num = seq_num;
    transmit_frame(nack);
}

bool ReceiverTransport::transmit_frame(const Frame& frame) {
    uint8_t tx_buffer[64]; // Служебные ACK/NACK маленькие, памяти нужно минимум
    size_t raw_size = PREAMBLE_LEN + FrameHeader::size() + 2; // Без payload

    size_t idx = 0;
    for (size_t i = 0; i < PREAMBLE_LEN; ++i) {
        tx_buffer[idx++] = PREAMBLE_BYTES[i];
    }

    tx_buffer[idx++] = frame.version;
    tx_buffer[idx++] = static_cast<uint8_t>(frame.type);
    tx_buffer[idx++] = frame.seq_num;

    // Длина payload = 0
    tx_buffer[idx++] = 0;
    tx_buffer[idx++] = 0;

    // Считаем CRC для заголовка служебного пакета
    uint16_t crc = 0xFFFF;
    crc = Crc16::calculate(tx_buffer + PREAMBLE_LEN, FrameHeader::size(), crc);

    tx_buffer[idx++] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    tx_buffer[idx++] = static_cast<uint8_t>(crc & 0xFF);

    return tx_cb_(tx_buffer, idx);
}
} 