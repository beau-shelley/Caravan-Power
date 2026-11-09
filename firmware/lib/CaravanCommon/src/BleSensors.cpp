#include "BleSensors.h"

#include <cstring>

namespace cv {

namespace {
inline int16_t s16le(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }
inline uint16_t u16le(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool plausible(const BleSensorReading &r) {
  if (r.hasTemp && (r.tempC < -45 || r.tempC > 90)) return false;
  if (r.hasHum && (r.humidity < 0 || r.humidity > 100.5f)) return false;
  return r.hasTemp;
}

// BTHome v2 object id -> payload length (0 = unknown, 0xFF = length-prefixed).
uint8_t bthomeLen(uint8_t id) {
  if (id >= 0x15 && id <= 0x2D) return 1;  // binary sensors
  switch (id) {
    case 0x00: case 0x01: case 0x09: case 0x0F: case 0x10: case 0x11: case 0x2E: case 0x2F:
    case 0x3A: case 0x46: case 0x57: case 0x58: case 0x59: case 0x60:
      return 1;
    case 0x02: case 0x03: case 0x06: case 0x07: case 0x08: case 0x0C: case 0x0D: case 0x0E:
    case 0x12: case 0x13: case 0x14: case 0x3C: case 0x3D: case 0x3F: case 0x40: case 0x41:
    case 0x43: case 0x44: case 0x45: case 0x47: case 0x48: case 0x49: case 0x4A: case 0x51:
    case 0x52: case 0x56: case 0x5A: case 0x5D: case 0x5E: case 0x5F: case 0xF0:
      return 2;
    case 0x04: case 0x05: case 0x0A: case 0x0B: case 0x42: case 0x4B: case 0xF2:
      return 3;
    case 0x3E: case 0x4C: case 0x4D: case 0x4E: case 0x4F: case 0x50: case 0x55: case 0x5B:
    case 0x5C: case 0xF1:
      return 4;
    case 0x53: case 0x54:
      return 0xFF;
    default:
      return 0;
  }
}

bool decodeBthome(const uint8_t *d, size_t len, BleSensorReading &r) {
  if (len < 2) return false;
  uint8_t info = d[0];
  if (info & 0x01) return false;          // encrypted - turn encryption off on the sensor
  if ((info >> 5) != 2) return false;     // BTHome v2 only
  r.type = "BTHome";
  size_t i = 1;
  while (i < len) {
    uint8_t id = d[i++];
    uint8_t n = bthomeLen(id);
    if (n == 0) break;  // unknown object: keep what we have
    if (n == 0xFF) {
      if (i >= len) break;
      n = d[i++];
    }
    if (i + n > len) break;
    const uint8_t *p = d + i;
    switch (id) {
      case 0x01: r.hasBatt = true; r.battPct = p[0]; break;
      case 0x02: r.hasTemp = true; r.tempC = s16le(p) * 0.01f; break;
      case 0x45: r.hasTemp = true; r.tempC = s16le(p) * 0.1f; break;
      case 0x57: r.hasTemp = true; r.tempC = (int8_t)p[0]; break;
      case 0x58: r.hasTemp = true; r.tempC = (int8_t)p[0] * 0.35f; break;
      case 0x03: r.hasHum = true; r.humidity = u16le(p) * 0.01f; break;
      case 0x2E: r.hasHum = true; r.humidity = p[0]; break;
      default: break;
    }
    i += n;
  }
  return plausible(r);
}
}  // namespace

bool decodeBleServiceData(uint16_t uuid, const uint8_t *d, size_t len, BleSensorReading &r) {
  r = BleSensorReading();
  if (uuid == 0xFCD2) return decodeBthome(d, len, r);
  if (uuid == 0x181A) {
    if (len == 15 || len == 16) {  // pvvx custom: little-endian, 0.01 units
      r.type = "pvvx";
      r.hasTemp = r.hasHum = r.hasBatt = true;
      r.tempC = s16le(d + 6) * 0.01f;
      r.humidity = u16le(d + 8) * 0.01f;
      r.battPct = d[12];
      return plausible(r);
    }
    if (len == 13) {  // ATC1441: big-endian temperature in 0.1 C
      r.type = "ATC";
      r.hasTemp = r.hasHum = r.hasBatt = true;
      r.tempC = (int16_t)((d[6] << 8) | d[7]) * 0.1f;
      r.humidity = d[8];
      r.battPct = d[9];
      return plausible(r);
    }
  }
  return false;
}

bool decodeBleManufacturerData(const char *name, const uint8_t *m, size_t len, BleSensorReading &r) {
  r = BleSensorReading();
  if (len < 3) return false;
  uint16_t company = u16le(m);

  // Govee H5075 family: 88 EC 00 <24-bit packed temp+hum, BE> <battery %> [..]
  if (company == 0xEC88 && (len == 8 || len == 9) && m[2] == 0x00) {
    uint32_t packed = ((uint32_t)m[3] << 16) | ((uint32_t)m[4] << 8) | m[5];
    bool neg = packed & 0x800000;
    packed &= 0x7FFFFF;
    r.type = "Govee";
    r.hasTemp = r.hasHum = r.hasBatt = true;
    r.tempC = (packed / 1000) / 10.0f * (neg ? -1 : 1);
    r.humidity = (packed % 1000) / 10.0f;
    r.battPct = m[6] & 0x7F;
    return plausible(r);
  }

  // ThermoPro TP35x/TP39x: the "company id" field is really data. Byte 0 is
  // a flag, bytes 1-2 temperature (int16 LE, 0.1 C), byte 3 humidity %.
  if (name && (!strncmp(name, "TP35", 4) || !strncmp(name, "TP39", 4)) && len >= 4) {
    r.type = "ThermoPro";
    r.hasTemp = r.hasHum = true;
    r.tempC = s16le(m + 1) * 0.1f;
    r.humidity = m[3];
    return plausible(r);
  }
  return false;
}

}  // namespace cv
