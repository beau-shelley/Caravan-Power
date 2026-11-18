#include "BmproLink.h"

namespace cv {
namespace bmpro {

namespace {
int32_t be32(const uint8_t *p) {
  return (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]);
}
}  // namespace

bool parse(const uint8_t *d, size_t n, Msg &m) {
  m = Msg();
  if (!d || n < 5) return false;
  m.op = d[0];
  m.node = d[2];
  m.param = d[3];
  m.len = d[4];
  if (n < 5u + m.len) return false;
  m.data = d + 5;
  return true;
}

bool apply(const Msg &m, Values &v) {
  if (m.op != OP_VALUE && m.op != OP_WRITE_REPLY) return false;
  if (m.node == NODE_POWER) {
    if ((m.param == P_PUMP1 || m.param == P_PUMP2) && m.len >= 1) {
      int i = m.param - P_PUMP1;
      v.hasPump[i] = true;
      v.pumpOn[i] = m.data[0] != 0;
      return true;
    }
    if (m.len != 4) return false;
    int32_t x = be32(m.data);
    switch (m.param) {
      case P_BAT_MA: v.hasBatA = true; v.batA = x / 1000.0f; return true;
      case P_BAT_MV: v.hasBatV = true; v.batV = x / 1000.0f; return true;
      case P_V2_MV: v.hasV2 = true; v.v2 = x / 1000.0f; return true;
      case P_SOLAR_MA: v.hasSolarA = true; v.solarA = x / 1000.0f; return true;
      case P_SOC:
        if (x < 0 || x > 100) return false;
        v.hasSoc = true;
        v.soc = (float)x;
        return true;
      case P_MIN_LEFT: v.hasMinLeft = true; v.minLeft = x; return true;
      default: return false;
    }
  }
  if (m.node == NODE_SENSORS && m.param == P_TANKS && m.len == 4) {
    // Seen: 00 02 04 04 with the app showing Top Full, Bottom Full, Grey 1/2.
    for (int i = 0; i < 4; i++) {
      uint8_t q = m.data[3 - i];
      if (q > 4) return false;
      v.tankPct[i] = q * 25.0f;
    }
    v.hasTanks = true;
    return true;
  }
  return false;
}

size_t encodePump(int pump, bool on, uint8_t *buf) {
  buf[0] = OP_WRITE;
  buf[1] = 0x00;
  buf[2] = NODE_POWER;
  buf[3] = pump ? P_PUMP2 : P_PUMP1;
  buf[4] = 0x01;
  buf[5] = on ? 0x01 : 0x00;
  return 6;
}

}  // namespace bmpro
}  // namespace cv
