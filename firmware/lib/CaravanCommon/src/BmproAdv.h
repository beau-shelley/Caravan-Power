// BMPRO broadcasts (service data, 16-bit UUID 0xFE0E), decoded from a van
// test on 2026-11-17 (guided survey log). Listen-only: no connection needed.
//
// Service data after the UUID, 18 bytes, first byte = frame type:
//   0x00  identity  : 01 07 00 41 20 41 24 40 30 36 00 29 | uptime s (u32 LE)
//   0x01  battery   : volts mV (u16 LE) | current mA (i32 LE, - = discharging)
//                     | remaining mAh (u32 LE) | b11 b12 (unknown: 1b 18) | uptime s (u32 LE)
//   0x04  outputs   : b1-2 (0000/ffff, unknown) | ... | b14-15 load current mA (u16 LE)
//                     | b16-17 unknown (u16 LE)
// Confirmed against deliberate load switching (-26 A / 23.7 A, Ah falling by
// exactly the charge drawn). b11/b12 and the type 0x04 extras are not yet known.
// Portable (no Arduino deps) so it is unit tested on the host.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cv {

constexpr uint16_t BMPRO_SVC_UUID = 0xFE0E;

struct BmproAdvert {
  uint8_t frame = 0xFF;  // 0 identity, 1 battery, 4 outputs, other = unknown
  // frame 1
  bool hasBattery = false;
  float batV = 0, batA = 0, remainAh = 0;
  uint8_t b11 = 0, b12 = 0;
  // frame 4
  bool hasLoad = false;
  float loadA = 0;
  uint16_t outFlags = 0, outExtra = 0;
  // frames 0 and 1
  uint32_t uptimeS = 0;
  // raw service data (after the UUID)
  const uint8_t *data = nullptr;
  size_t len = 0;
};

// raw = whole advert (AD structures). False if it has no 0xFE0E service data.
bool bmproParseAdvert(const uint8_t *raw, size_t len, BmproAdvert &out);
// Same, given just the service data that follows the UUID.
bool bmproParseServiceData(const uint8_t *d, size_t len, BmproAdvert &out);

}  // namespace cv
