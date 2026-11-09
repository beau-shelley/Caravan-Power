// Bluetooth thermometers (ThermoPro, Govee, Shelly BLU / BTHome, Xiaomi pvvx)
// for any of the inside / outside / fridge roles. Passive listening only.
#pragma once
#include <Arduino.h>
#include "CaravanModel.h"

namespace hub {

class BleTempRx {
 public:
  void begin();
  void poll(cv::State &s);   // fills roles configured as Bluetooth
  String seen() const;       // JSON list of thermometers in range
};

extern BleTempRx gBleTemps;

}  // namespace hub
