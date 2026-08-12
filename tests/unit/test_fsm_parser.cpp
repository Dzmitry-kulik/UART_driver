#include "FSM_parser.hpp"
#include "crc16.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using namespace protocol;

// Мокирование системного таймера HAL_GetTick
uint32_t g_mock_tick = 0;
extern "C" uint32_t HAL_GetTick(void) { return g_mock_tick; }

DiagnosticsStats g_stats{};
int g_frames_ok = 0;
int g_errors_cnt = 0;

void test_callback(const std::expected<Frame, ParseError> &result) {
  if (result.has_value()) {
    g_frames_ok++;
  } else {
    g_errors_cnt++;
  }
}

// Передаем type как uint8_t, чтобы не завязываться на названия enum
std::vector<uint8_t> make_frame(uint8_t version, uint8_t type, uint8_t seq,
                                const std::vector<uint8_t> &payload,
                                bool corrupt_crc = false) {
  std::vector<uint8_t> frame = {0xAA, 0x55, version, type, seq};
  uint16_t len = payload.size();
  frame.push_back((len >> 8) & 0xFF);
  frame.push_back(len & 0xFF);
  frame.insert(frame.end(), payload.begin(), payload.end());

  uint16_t crc = Crc16::calculate(&frame[2], 5 + len);
  if (corrupt_crc) {
    crc ^= 0xFFFF;
  }

  frame.push_back((crc >> 8) & 0xFF);
  frame.push_back(crc & 0xFF);
  return frame;
}

int main() {
  FrameParser parser(g_stats, test_callback);

  constexpr uint8_t MSG_DATA = 0x01;
  constexpr uint8_t MSG_COMMAND = 0x10;

  // 1. Проверка обработки nullptr
  parser.process_buffer(nullptr, 10);

  // 2. Успешный кадр без payload
  auto f1 = make_frame(0x01, MSG_COMMAND, 1, {});
  parser.process_buffer(f1.data(), f1.size());
  assert(g_frames_ok == 1);

  // 3. Успешный кадр с данными
  auto f2 = make_frame(0x01, MSG_DATA, 2, {0x01, 0x02, 0x03, 0x04});
  parser.process_buffer(f2.data(), f2.size());
  assert(g_frames_ok == 2);

  // 4. Ошибка CRC
  auto f3 = make_frame(0x01, MSG_DATA, 3, {0xAA}, true);
  parser.process_buffer(f3.data(), f3.size());
  assert(g_errors_cnt == 1);
  assert(g_stats.crc_errors == 1);

  // 5. Ошибка длины (Payload > 512 байт)
  std::vector<uint8_t> oversized = {0xAA, 0x55, 0x01, 0x01,
                                    0x04, 0x02, 0x01}; // len = 513
  parser.process_buffer(oversized.data(), oversized.size());
  assert(g_stats.length_errors == 1);

  // 6. Межбайтовый таймаут
  auto f5 = make_frame(0x01, MSG_DATA, 5, {0x11, 0x22});
  parser.process_buffer(f5.data(), 4);
  g_mock_tick += 500;
  parser.check_timeout(g_mock_tick, 100);
  assert(g_stats.timeout_errors == 1);

  // 7. Повторный приём после таймаута
  parser.process_buffer(f5.data(), f5.size());
  assert(g_frames_ok == 3);

  // 8. Повторяющаяся преамбула (0xAA 0xAA 0x55)
  std::vector<uint8_t> double_preamble = {0xAA, 0xAA, 0x55, 0x01,
                                          0x01, 0x06, 0x00, 0x00};
  uint16_t crc = Crc16::calculate(&double_preamble[3], 5);
  double_preamble.push_back((crc >> 8) & 0xFF);
  double_preamble.push_back(crc & 0xFF);
  parser.process_buffer(double_preamble.data(), double_preamble.size());
  assert(g_frames_ok == 4);

  // 9. Мусор перед преамбулой
  std::vector<uint8_t> garbage = {0x12, 0x34, 0xFF, 0xAA, 0x11};
  auto f7 = make_frame(0x01, MSG_COMMAND, 7, {0x77});
  garbage.insert(garbage.end(), f7.begin(), f7.end());
  parser.process_buffer(garbage.data(), garbage.size());
  assert(g_frames_ok == 5);

  std::cout << "SUCCESS: All host unit tests passed." << std::endl;
  return 0;
}
