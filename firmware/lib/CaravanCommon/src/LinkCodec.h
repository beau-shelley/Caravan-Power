// Line framing for the hub <-> display RS485 link.
// Each frame is one JSON document followed by "*HH\n" where HH is the XOR of
// every JSON byte (NMEA style). Corrupt or truncated lines are dropped.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cv {

// Writes "*HH\n" for payload into out (needs 5 bytes incl. NUL).
void linkTrailer(const char *payload, size_t len, char out[5]);

class LinkReader {
 public:
  static constexpr size_t MAX_LINE = 6144;
  // Feed one byte; returns true when a valid frame is ready in line()/length().
  bool feed(uint8_t b);
  const char *line() const { return buf_; }
  size_t length() const { return len_; }
  uint32_t errors() const { return errors_; }

 private:
  char buf_[MAX_LINE + 1];
  size_t pos_ = 0;
  size_t len_ = 0;
  bool overflow_ = false;
  uint32_t errors_ = 0;
};

}  // namespace cv
