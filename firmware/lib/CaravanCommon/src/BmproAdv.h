// BMPRO broadcasts (service data, 16-bit UUID 0xFE0E), decoded from a van
// test on 2026-11-17 (guided survey log). Listen-only: no connection needed.
//
// Service data after the UUID, 18 bytes, first byte = frame type:
//   0x00  identity  : 01 07 00 41 20 41 24 40 30 36 00 29 | uptime s (u32 LE)
//   0x01  battery   : volts mV (u16 LE) | current mA (i32 LE, - = discharging)
//                     | a mAh counter (u32 LE; moves with charge, but read 78 Ah on a full
//                       405 Ah bank, so not "Ah left") | b11 b12 (unknown: 1b 18) | uptime s
//   0x04  charging  : b1-2 (0000/ffff, unknown) | ... | b14-15 mA (u16 LE): tracked
//                     the battery current's size, not the app's "Loads"
//                     | b16-17 input (solar/charger) current, 0.1 A (u16 LE):
//                     10.5-13.5 A while the app showed Solar 12.2 A
// Battery confirmed against deliberate load switching (-26 A) and the Trek 3 /
// app (13.7 V). App "Loads" = input + battery discharge (12.2 + 2.9 = 15.0).
// Portable (no Arduino deps) so it is unit tested on the host.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cv {

constexpr uint16_t BMPRO_SVC_UUID = 0xFE0E;

struct BmproAdvert {
  uint8_t frame = 0xFF;  // 0 identity, 1 battery, 4 charging, other = unknown
  // frame 1
  bool hasBattery = false;
  float batV = 0, batA = 0, remainAh = 0;
  uint8_t b11 = 0, b12 = 0;
  // frame 4
  bool hasInput = false;
  float inputA = 0;   // solar / charger input into the BMPRO
  float b14A = 0;     // unclear: followed the battery current's size
  uint16_t outFlags = 0;
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
