// Receives Victron Instant Readout broadcasts from the two SmartSolar
// controllers - no cables, no pairing, VictronConnect keeps working.
#pragma once
#include <Arduino.h>
#include "CaravanModel.h"

namespace hub {

class VictronBleRx {
 public:
  void begin();
  // keyHex: 32 hex chars from VictronConnect. addr optional ("" = match by key).
  bool setDevice(int i, const char *addr, const char *keyHex);
  void poll(int i, cv::Solar &out);  // copies fresh data; offline after 30 s silence
  void resetDaily();                 // peak power is tracked here, reset at midnight
  String seen() const;               // JSON list of Victron devices in range
};

extern VictronBleRx gVictronBle;

}  // namespace hub
