// Minimal Arduino shim so the display UI can be compiled and rendered on a PC.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif
uint32_t millis(void);
void delay(uint32_t ms);
#ifdef __cplusplus
}

#include <algorithm>
#include <string>
using std::max;
using std::min;

template <class T, class L, class H>
static inline T constrain(T x, L lo, H hi) { return x < lo ? (T)lo : x > hi ? (T)hi : x; }

class String {
 public:
  String(const char *c = "") : s_(c ? c : "") {}
  String(const std::string &s) : s_(s) {}
  const char *c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  String &operator+=(const char *c) { s_ += c; return *this; }
  String &operator+=(const String &o) { s_ += o.s_; return *this; }
  String operator+(const char *c) const { return String(s_ + c); }
  bool operator==(const char *c) const { return s_ == c; }
  // Lets ArduinoJson serialise straight into a String (custom writer API).
  size_t write(uint8_t c) { s_.push_back((char)c); return 1; }
  size_t write(const uint8_t *p, size_t n) { s_.append((const char *)p, n); return n; }
 private:
  std::string s_;
};

#define SERIAL_8N1 0
struct HardwareSerial {
  void begin(unsigned long, int = 0, int = -1, int = -1) {}
  void setRxBufferSize(size_t) {}
  int available() { return 0; }
  int read() { return -1; }
  size_t write(const uint8_t *, size_t n) { return n; }
  template <class... A> void printf(const char *f, A... a) { ::printf(f, a...); }
  void println(const char *s) { ::puts(s); }
};
extern HardwareSerial Serial, Serial1;
#endif
