#pragma once

#include "diagnostics.hpp"
#include "frame.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>

namespace protocol {

enum class ParseError : uint8_t {
  NONE = 0,
  INVALID_CRC,
  PAYLOAD_TOO_LARGE,
  FRAME_INCOMPLETE,
  UNEXPECTED_BYTE
};

enum class ParserState { WAIT_SYNC, HEADER, PAYLOAD, CRC, DISPATCH };

using FrameCallback = std::function<void(std::expected<Frame, ParseError>)>;

class FrameParser {
public:
  explicit FrameParser(DiagnosticsStats &stats,
                       FrameCallback on_frame_cb = nullptr);

  std::expected<void, ParseError> process_byte(uint8_t byte);

  void process_buffer(const uint8_t *buffer, size_t size);
  void reset();
  void set_callback(FrameCallback cb) { on_frame_cb_ = cb; }
  void check_timeout(uint32_t current_time_ms, uint32_t timeout_ms = 50);

  ParserState get_state() const { return state_; }

private:
  ParserState state_{ParserState::WAIT_SYNC};
  DiagnosticsStats &stats_;
  FrameCallback on_frame_cb_;
  uint32_t last_byte_time_ms_ = 0;
  Frame rx_frame_{};
  size_t bytes_read_{0};
  uint8_t sync_index_{0};
  uint16_t rx_crc_{0};

  void transition_to(ParserState new_state);
  bool validate_crc();
};

} // namespace protocol
