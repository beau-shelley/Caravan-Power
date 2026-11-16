// Works out what a Bluetooth advert probably is, from its raw bytes alone,
// so the listen-only survey log can label every line ("victron-solar",
// "bmpro?", "heater?", "thermometer"...) and skip the phones, laptops and
// earbuds that fill a caravan park.
//
// Raw advert = the AD structures as received: [len][type][data...] repeated.
// Portable (no Arduino deps) so it is unit tested on the host.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cv {

enum BleKind : uint8_t {
  BLE_UNKNOWN = 0,
  BLE_VICTRON,      // Victron Instant Readout (encrypted, needs the device key)
  BLE_BMPRO,        // name looks like a BMPRO product (Odyssey, CommLink, SmartSense, BC300...)
  BLE_HEATER,       // FFE0 service or "heater" in the name (AirHeaterBLE style)
  BLE_THERMOMETER,  // a thermometer the hub can already decode
  BLE_NOISE,        // phones, laptops, earbuds, trackers: not ours, not logged
};

struct BleTag {
  BleKind kind = BLE_UNKNOWN;
  char label[24] = {0};   // short word for the log, e.g. "victron-solar"
  char name[24] = {0};    // advertised name, if any
  uint16_t mfrId = 0;     // manufacturer company id (0 = none)
  const uint8_t *mfr = nullptr;  // manufacturer data incl. the 2-byte id, points into raw
  size_t mfrLen = 0;
  // Thermometer reading when kind == BLE_THERMOMETER.
  bool hasTemp = false;
  float tempC = 0;
};

// Parses raw and fills tag. Always returns a usable tag (BLE_UNKNOWN at worst).
void bleClassify(const uint8_t *raw, size_t len, BleTag &tag);

// Victron record type -> word for the log ("solar", "battery", "inverter", "dcdc").
const char *victronRecordName(uint8_t record);

}  // namespace cv
