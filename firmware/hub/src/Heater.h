// Diesel heater control (Power Science 2 kW in the van).
//
// Every way of reaching the heater sits behind one small interface, so the
// rest of the hub (commands, screens, phone) doesn't care which is used:
//   * HeaterBle  - Bluetooth, the AirHeaterBLE app's protocol (cv::airheater).
//                  Two-way: on/off, mode, set point, plus full status.
//   * Heater433  - 433 MHz remote codes through a CC1101 module in the hub
//                  box. STUB: not built yet. One-way, so no status at all.
//   * demo heater (Demo.cpp) - simulated, for the fake-data bench build.
// The protocol itself is assumed until confirmed from the van; see
// docs/HEATER.md and AirHeater.h.
#pragma once
#include <Arduino.h>

#include "AirHeater.h"
#include "CaravanModel.h"

namespace hub {

class HeaterDriver {
 public:
  virtual ~HeaterDriver() {}
  // Fills s.htr (phase, set point, readings, online). Called every loop.
  virtual void loop(cv::State &s) = 0;
  // Sends a command. false + *why when it can't (shown to the user).
  virtual bool send(const cv::HeaterCmd &c, const cv::Heater &cur, const char **why) = 0;
};

// ---------------------------------------------------------------- Bluetooth
class HeaterBle : public HeaterDriver {
 public:
  // Starts the BLE client task (once) and (re)targets it. addr "" = only
  // listen for heaters nearby (heater_seen) without connecting.
  void setTarget(const char *addr, uint16_t passkey);
  void release();  // disconnect and stop reconnecting (no-op if never started)
  void loop(cv::State &s) override;
  bool send(const cv::HeaterCmd &c, const cv::Heater &cur, const char **why) override;
  bool connected() const;
  // Starts listening for heater adverts (for the phone's "Find nearby").
  void listen();
  String seen() const;  // JSON array of nearby heater-looking devices
  String log() const;   // recent frames, newest last, for confirming the protocol
};

// ---------------------------------------------------------------- 433 MHz
// Placeholder for a CC1101 transmitter replaying the heater's 433 MHz remote.
// The remote's codes have to be captured first (same CC1101, receive mode).
class Heater433 : public HeaterDriver {
 public:
  void loop(cv::State &s) override;
  bool send(const cv::HeaterCmd &c, const cv::Heater &cur, const char **why) override;
};

extern HeaterBle gHeaterBle;
extern Heater433 gHeater433;

}  // namespace hub
