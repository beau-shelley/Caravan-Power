// BMPRO OdysseyLink integration over Bluetooth LE.
//
// The OdysseyLink is BMPRO's Bluetooth gateway. The official Odyssey app uses
// it to show battery and tank levels and to switch the water pumps, so the hub
// can do the same thing wirelessly: no tank sender taps, no pump relays, and
// the Trek 3 screen is no longer needed.
//
// BMPRO does not publish the protocol. This module therefore:
//  * keeps a permanent BLE connection to the OdysseyLink (auto-reconnect),
//  * subscribes to every notify/indicate characteristic,
//  * logs every frame + advert so the protocol can be decoded,
//  * lets you replay writes captured from the phone app (bmpro_write),
//  * routes decoded data into the system via two functions you fill in:
//      Bmpro::decode()      frames -> battery / tank / pump values
//      Bmpro::encodePump()  pump on/off -> bytes to write
// See docs/BMPRO_INTEGRATION.md for the capture procedure.
#pragma once
#include <Arduino.h>
#include "CaravanModel.h"

namespace hub {

// Everything a decoded frame may carry. Only fields with *Valid set are used.
struct BmproData {
  bool batValid = false;
  float batV = 0, batA = 0, batSoc = 0;
  float remainAh = -1;  // from the broadcast; -1 = unknown
  int32_t batTtgMin = -1;
  bool chargerValid = false;
  float chargerA = 0;   // BMPRO charger output (solar input + DC-DC)
  bool loadValid = false;  // BMPRO 12 V load outputs (lets the mains-charger figure be exact)
  float loadA = 0;
  bool tankValid[cv::NUM_TANKS] = {};
  float tankPct[cv::NUM_TANKS] = {};
  bool gasValid = false;  // SmartSense LPG level (frame not decoded yet: stub)
  float gasPct = 0;
  bool pumpValid[cv::NUM_PUMPS] = {};
  bool pumpOn[cv::NUM_PUMPS] = {};
};

class Bmpro {
 public:
  // addr: OdysseyLink BLE address ("" = scan only). verbose: log every frame.
  void begin(const char *addr, bool verbose);
  // Copies freshly decoded values into the state; marks stale data offline.
  void loop(cv::State &s, bool useBattery, bool useTanks, bool usePumps);
  void connect(const char *addr);  // change target (async)
  // The newest battery reading, once each (false if nothing new). bmproSoc is
  // the BMPRO's own % over the connection, or -1 (broadcasts don't carry one).
  bool takeBattery(float &v, float &a, uint32_t &ms, float &bmproSoc);
  bool connected() const;
  bool decoded() const;  // any frame decoded yet?
  // Load current measured by the BMPRO, or -1 if not decoded / stale.
  float loadA() const;
  bool setPump(int i, bool on, const char **why);
  // Raw write for protocol experiments, e.g. replaying a captured app write.
  bool write(const char *charUuid, const uint8_t *data, size_t len, bool response);
  String log() const;
  String devices() const;

  // ---- protocol hooks (fill these in once frames are understood) ----------
  static bool decode(const char *charUuid, const uint8_t *data, size_t len, BmproData &out);
  static bool encodePump(int pump, bool on, String &charUuid, uint8_t *buf, size_t &len);
};

extern Bmpro gBmpro;

}  // namespace hub
