#include "LinkCodec.h"

namespace cv {

static const char kHex[] = "0123456789ABCDEF";

void linkTrailer(const char *payload, size_t len, char out[5]) {
  uint8_t x = 0;
  for (size_t i = 0; i < len; i++) x ^= (uint8_t)payload[i];
  out[0] = '*';
  out[1] = kHex[x >> 4];
  out[2] = kHex[x & 15];
  out[3] = '\n';
  out[4] = 0;
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool LinkReader::feed(uint8_t b) {
  if (b == '\r') return false;
  if (b != '\n') {
    if (pos_ < MAX_LINE) buf_[pos_++] = (char)b;
    else overflow_ = true;
    return false;
  }
  // end of line
  size_t n = pos_;
  pos_ = 0;
  if (overflow_ || n < 4 || buf_[n - 3] != '*') {
    if (n > 0 || overflow_) errors_++;
    overflow_ = false;
    return false;
  }
  int hi = hexVal(buf_[n - 2]), lo = hexVal(buf_[n - 1]);
  uint8_t x = 0;
  for (size_t i = 0; i < n - 3; i++) x ^= (uint8_t)buf_[i];
  if (hi < 0 || lo < 0 || x != (uint8_t)((hi << 4) | lo)) {
    errors_++;
    return false;
  }
  len_ = n - 3;
  buf_[len_] = 0;
  return true;
}

}  // namespace cv
