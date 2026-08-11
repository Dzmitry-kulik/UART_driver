#pragma once

#include <cstddef>
#include <cstdint>

namespace protocol {

/**
 * @brief Потокобезопасный (SPSC) кольцевой буфер для байтовых данных.
 * Предназначен для обмена данными между ISR (прерыванием) и основным циклом
 * (main).
 */
class CircularBuffer {
public:
  /**
   * @brief Конструктор с внешним буфером (без выделения динамической памяти).
   * @param buffer Указатель на выделенный массив памяти.
   * @param capacity Размер буфера (желательно кратный степени двойки: 128, 256,
   * 512, 1024).
   */
  CircularBuffer(uint8_t *buffer, size_t capacity) noexcept;

  // Запрет копирования для безопасности ресурсов МК
  CircularBuffer(const CircularBuffer &) = delete;
  CircularBuffer &operator=(const CircularBuffer &) = delete;

  /**
   * @brief Запись одного байта в буфер (потокобезопасно для Single-Producer).
   * @param byte Записываемый байт.
   * @return true - успешно, false - буфер переполнен.
   */
  bool push(uint8_t byte) noexcept;

  /**
   * @brief Чтение одного байта из буфера (потокобезопасно для Single-Consumer).
   * @param byte Ссылка для сохранения прочитанного байта.
   * @return true - успешно, false - буфер пуст.
   */
  bool pop(uint8_t &byte) noexcept;

  /**
   * @brief Проверка буфера на пустоту.
   */
  [[nodiscard]] bool empty() const noexcept;

  /**
   * @brief Проверка буфера на переполнение.
   */
  [[nodiscard]] bool full() const noexcept;

  /**
   * @brief Текущее количество байт в буфере.
   */
  [[nodiscard]] size_t size() const noexcept;

  /**
   * @brief Максимальная вместимость буфера.
   */
  [[nodiscard]] size_t capacity() const noexcept;

  /**
   * @brief Сброс индексов чтения и записи.
   */
  void clear() noexcept;

private:
  uint8_t *const buffer_;
  const size_t capacity_;
  const size_t mask_;
  const bool is_power_of_two_;

  volatile size_t head_{0}; // Индекс записи (Producer)
  volatile size_t tail_{0}; // Индекс чтения (Consumer)
};

} // namespace protocol
