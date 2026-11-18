#include "BmproAdv.h"

namespace cv {

namespace {
uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
}  // namespace

bool bmproParseServiceData(const uint8_t *d, size_t len, BmproAdvert &out) {
  out = BmproAdvert();
  if (!d || len < 1) return false;
  out.data = d;
  out.len = len;
  out.frame = d[0];
  if (out.frame == 0x01 && len == 18) {
    out.batV = u16(d + 1) / 1000.0f;
    out.batA = (int32_t)u32(d + 3) / 1000.0f;
    out.remainAh = u32(d + 7) / 1000.0f;
    out.b11 = d[11];
    out.b12 = d[12];
    out.uptimeS = u32(d + 13);
    // Plausibility: a 12/24 V house battery and a sane shunt reading.
    out.hasBattery = out.batV > 8 && out.batV < 32 && out.batA > -1000 && out.batA < 1000;
  } else if (out.frame == 0x04 && len == 18) {  // other BMPRO models send longer frames
    out.outFlags = u16(d + 1);
    out.b14A = u16(d + 14) / 1000.0f;
    out.inputA = u16(d + 16) / 10.0f;
    out.hasInput = true;
  } else if (out.frame == 0x00 && len == 18) {
    out.uptimeS = u32(d + 13);
  }
  return true;
}

bool bmproParseAdvert(const uint8_t *raw, size_t len, BmproAdvert &out) {
  for (size_t i = 0; i < len;) {
    uint8_t l = raw[i];
    if (l == 0 || i + 1 + l > len) break;
    if (raw[i + 1] == 0x16 && l >= 3 && u16(raw + i + 2) == BMPRO_SVC_UUID)
      return bmproParseServiceData(raw + i + 4, l - 3, out);
    i += 1 + l;
  }
  out = BmproAdvert();
  return false;
}

}  // namespace cv
