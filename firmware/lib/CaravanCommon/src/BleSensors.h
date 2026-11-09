// Decoders for off-the-shelf Bluetooth thermometers that broadcast their
// readings in plain (unencrypted) advertisements. Passive: no pairing.
//
//  * BTHome v2 (service data 0xFCD2)  - Shelly BLU H&T, Xiaomi LYWSD03MMC /
//                                        MHO-C401 etc. on pvvx firmware (BTHome mode)
//  * pvvx custom / ATC1441 (0x181A)   - Xiaomi sensors on pvvx / ATC firmware
//  * Govee H5075 family (mfg 0xEC88)  - H5075, H5072, H5101, H5102, H5177
//  * ThermoPro TP357/358/359/393      - name "TP35x"/"TP39x", temp in mfg data
// Portable (no Arduino deps) so it is unit tested on the host.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cv {

struct BleSensorReading {
  bool hasTemp = false, hasHum = false, hasBatt = false;
  float tempC = 0, humidity = 0;
  uint8_t battPct = 0;
  const char *type = "";
};

// uuid16 = 16-bit service UUID of a service-data entry.
bool decodeBleServiceData(uint16_t uuid16, const uint8_t *d, size_t len, BleSensorReading &out);
// mfg = manufacturer data INCLUDING the 2-byte company id; name may be "".
bool decodeBleManufacturerData(const char *name, const uint8_t *mfg, size_t len, BleSensorReading &out);

}  // namespace cv
