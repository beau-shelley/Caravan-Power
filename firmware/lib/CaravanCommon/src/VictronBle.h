// Victron "Instant Readout" Bluetooth advertisements (SmartSolar, SmartShunt...).
//
// Victron devices broadcast live data in BLE advertisements, encrypted with
// AES-128-CTR using a per-device key shown in VictronConnect:
//   Product -> cog -> Product info -> Instant readout details -> Show
// No pairing or connection is needed, and VictronConnect keeps working.
//
// Manufacturer data (company id 0x02E1, little-endian), byte offsets after the id:
//   0     0x10 (product advertisement)
//   1-2   model id
//   3     readout type (0xA0)
//   4     record type (0x01 = solar charger)
//   5-6   nonce / data counter (LE)  -> CTR counter block = nonce as 128-bit LE int
//   7     first byte of the key (quick "is this my key" check)
//   8..   encrypted record
// Portable (no Arduino deps) so it is unit tested on the host.
#pragma once
#include <cstddef>
#include <cstdint>
#include "CaravanModel.h"

namespace cv {

constexpr uint16_t VICTRON_COMPANY_ID = 0x02E1;

enum VictronRecord : uint8_t {
  VREC_SOLAR_CHARGER = 0x01,
  VREC_BATTERY_MONITOR = 0x02,
  VREC_INVERTER = 0x03,
  VREC_DCDC = 0x04,
};

struct VictronAdvert {
  uint16_t model = 0;
  uint8_t record = 0;
  uint16_t nonce = 0;
  uint8_t keyCheck = 0;
  const uint8_t *enc = nullptr;
  size_t encLen = 0;
};

// FIPS-197 AES-128, encrypt direction only (all CTR mode needs).
void aes128Encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

// mfg = manufacturer data INCLUDING the 2-byte company id (as NimBLE returns it).
bool victronParseAdvert(const uint8_t *mfg, size_t len, VictronAdvert &out);
// Decrypts into out (up to 32 bytes). False if the key doesn't match.
bool victronDecrypt(const VictronAdvert &adv, const uint8_t key[16], uint8_t *out, size_t &outLen);
// Solar charger record -> Solar (PV voltage is not in the broadcast: pvV = -1).
bool victronDecodeSolar(const uint8_t *dec, size_t len, Solar &s);
// "0a1b2c..." (32 hex chars, spaces/colons ignored) -> 16-byte key.
bool victronKeyFromHex(const char *hex, uint8_t key[16]);

}  // namespace cv
