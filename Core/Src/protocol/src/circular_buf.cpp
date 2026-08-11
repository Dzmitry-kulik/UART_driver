#include "circular_buf.hpp"

namespace protocol {

namespace {
// Вспомогательная функция проверки степени двойки на этапе инициализации
constexpr bool check_power_of_two(size_t n) noexcept {
  return (n != 0) && ((n & (n - 1)) == 0);
}
} // namespace

CircularBuffer::CircularBuffer(uint8_t *buffer, size_t capacity) noexcept
    : buffer_(buffer), capacity_(capacity), mask_(capacity - 1),
      is_power_of_two_(check_power_of_two(capacity)) {}

bool CircularBuffer::push(uint8_t byte) noexcept {
  if (!buffer_) {
    return false;
  }

  size_t current_head = head_;
  size_t next_head;

  if (is_power_of_two_) {
    next_head = (current_head + 1) & mask_;
  } else {
    next_head = (current_head + 1) % capacity_;
  }

  // Если следующий индекс записи догнал индекс чтения — буфер переполнен
  if (next_head == tail_) {
    return false;
  }

  buffer_[current_head] = byte;

  // Компиляторный барьер памяти: гарантирует запись байта в ОЗУ до обновления
  // индекса
  asm volatile("" ::: "memory");

  head_ = next_head;
  return true;
}

bool CircularBuffer::pop(uint8_t &byte) noexcept {
  if (!buffer_) {
    return false;
  }

  size_t current_tail = tail_;

  // Если индексы равны — буфер пуст
  if (current_tail == head_) {
    return false;
  }

  byte = buffer_[current_tail];

  // Компиляторный барьер памяти: гарантирует чтение байта из ОЗУ до обновления
  // индекса
  asm volatile("" ::: "memory");

  if (is_power_of_two_) {
    tail_ = (current_tail + 1) & mask_;
  } else {
    tail_ = (current_tail + 1) % capacity_;
  }

  return true;
}

bool CircularBuffer::empty() const noexcept { return head_ == tail_; }

bool CircularBuffer::full() const noexcept {
  size_t next_head;
  if (is_power_of_two_) {
    next_head = (head_ + 1) & mask_;
  } else {
    next_head = (head_ + 1) % capacity_;
  }
  return next_head == tail_;
}

size_t CircularBuffer::size() const noexcept {
  size_t current_head = head_;
  size_t current_tail = tail_;

  if (current_head >= current_tail) {
    return current_head - current_tail;
  }
  return capacity_ - (current_tail - current_head);
}

size_t CircularBuffer::capacity() const noexcept {
  // Реальная полезная вместимость на 1 байт меньше для различия состояний
  // full/empty
  return capacity_ > 0 ? capacity_ - 1 : 0;
}

void CircularBuffer::clear() noexcept {
  head_ = 0;
  tail_ = 0;
}

} // namespace protocol
