#include "VictronBle.h"

#include <cstring>

namespace cv {

// ---------------------------------------------------------------- AES-128
namespace {
const uint8_t kSbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

inline uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }
}  // namespace

void aes128Encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  uint8_t rk[176];
  memcpy(rk, key, 16);
  uint8_t rcon = 1;
  for (int i = 16; i < 176; i += 4) {
    uint8_t t[4] = {rk[i - 4], rk[i - 3], rk[i - 2], rk[i - 1]};
    if (i % 16 == 0) {
      uint8_t tmp = t[0];
      t[0] = kSbox[t[1]] ^ rcon;
      t[1] = kSbox[t[2]];
      t[2] = kSbox[t[3]];
      t[3] = kSbox[tmp];
      rcon = xtime(rcon);
    }
    for (int j = 0; j < 4; j++) rk[i + j] = rk[i - 16 + j] ^ t[j];
  }
  uint8_t s[16];
  for (int i = 0; i < 16; i++) s[i] = in[i] ^ rk[i];
  for (int round = 1; round <= 10; round++) {
    uint8_t t[16];
    // SubBytes + ShiftRows (state is column-major: s[col*4 + row])
    for (int c = 0; c < 4; c++)
      for (int r = 0; r < 4; r++) t[c * 4 + r] = kSbox[s[((c + r) % 4) * 4 + r]];
    if (round != 10) {  // MixColumns
      for (int c = 0; c < 4; c++) {
        uint8_t *col = &t[c * 4];
        uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3], x = a0 ^ a1 ^ a2 ^ a3;
        col[0] ^= x ^ xtime(a0 ^ a1);
        col[1] ^= x ^ xtime(a1 ^ a2);
        col[2] ^= x ^ xtime(a2 ^ a3);
        col[3] ^= x ^ xtime(a3 ^ a0);
      }
    }
    for (int i = 0; i < 16; i++) s[i] = t[i] ^ rk[round * 16 + i];
  }
  memcpy(out, s, 16);
}

// ---------------------------------------------------------------- Victron
bool victronParseAdvert(const uint8_t *m, size_t len, VictronAdvert &out) {
  // company id (2) + header (8) + at least one encrypted byte
  if (len < 11) return false;
  if ((uint16_t)(m[0] | (m[1] << 8)) != VICTRON_COMPANY_ID) return false;
  const uint8_t *d = m + 2;
  if (d[0] != 0x10) return false;  // not a product advertisement
  out.model = (uint16_t)(d[1] | (d[2] << 8));
  out.record = d[4];
  out.nonce = (uint16_t)(d[5] | (d[6] << 8));
  out.keyCheck = d[7];
  out.enc = d + 8;
  out.encLen = len - 10;
  return true;
}

bool victronDecrypt(const VictronAdvert &adv, const uint8_t key[16], uint8_t *out, size_t &outLen) {
  if (adv.keyCheck != key[0] || adv.encLen == 0 || adv.encLen > 32) return false;
  uint8_t ctr[16] = {0};
  ctr[0] = adv.nonce & 0xFF;
  ctr[1] = adv.nonce >> 8;
  uint8_t ks[16];
  for (size_t i = 0; i < adv.encLen; i++) {
    if (i % 16 == 0) {
      aes128Encrypt(key, ctr, ks);
      for (int b = 0; b < 16 && ++ctr[b] == 0; b++) {
      }  // little-endian increment
    }
    out[i] = adv.enc[i] ^ ks[i % 16];
  }
  outLen = adv.encLen;
  return true;
}

bool victronDecodeSolar(const uint8_t *d, size_t len, Solar &s) {
  if (len < 10) return false;
  int16_t v = (int16_t)(d[2] | (d[3] << 8));
  int16_t a = (int16_t)(d[4] | (d[5] << 8));
  uint16_t yt = (uint16_t)(d[6] | (d[7] << 8));
  uint16_t pw = (uint16_t)(d[8] | (d[9] << 8));
  if (v == 0x7FFF) return false;  // "not available"
  s.cs = d[0];
  s.err = d[1];
  s.batV = v / 100.0f;
  s.batA = (a == 0x7FFF) ? 0 : a / 10.0f;
  if (yt != 0xFFFF) s.yieldTodayKwh = yt / 100.0f;
  s.pvW = (pw == 0xFFFF) ? 0 : (float)pw;
  s.pvV = -1;  // not broadcast
  if (s.pvW > s.maxWToday) s.maxWToday = (uint16_t)s.pvW;  // tracked locally
  return true;
}

bool victronKeyFromHex(const char *hex, uint8_t key[16]) {
  int n = 0, hi = -1;
  for (const char *p = hex; p && *p; p++) {
    char c = *p;
    int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
    if (v < 0) {
      if (c == ' ' || c == ':' || c == '-') continue;
      return false;
    }
    if (hi < 0) {
      hi = v;
    } else {
      if (n >= 16) return false;
      key[n++] = (uint8_t)((hi << 4) | v);
      hi = -1;
    }
  }
  return n == 16 && hi < 0;
}

}  // namespace cv
