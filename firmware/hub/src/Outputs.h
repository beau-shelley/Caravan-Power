// Controlled loads: water pumps, inverter (relays) and the aircon (IR).
#pragma once
#include <Arduino.h>
#include "CaravanModel.h"
#include "HubConfig.h"

namespace hub {

// A relay-driven device with optional opto "sense" feedback.
//  LATCH: relay closed = on. Wire the relay in parallel (diode-OR) with the
//         BMPRO pump output, so the BMPRO can still turn the pump on.
//  PULSE: a short closure toggles the device, like pressing its own button
//         (inverter remote port, or a BMPRO external switch input). The sense
//         input is then the source of truth for on/off.
class SwitchedOutput {
 public:
  void begin(const char *name, int outPin, int sensePin);
  void configure(SwitchMode mode, uint16_t pulseMs, bool useSense);
  // Request a state. Returns false (with reason) if refused.
  bool set(bool on, const char **reason = nullptr);
  void loop();
  bool commanded() const { return want_; }
  bool sensed() const { return sensed_; }
  bool hasSense() const { return useSense_ && sensePin_ >= 0; }
  // Best knowledge of whether the device is actually running.
  bool isOn() const { return hasSense() ? sensed_ : want_; }
  uint32_t onSinceMs() const { return onSinceMs_; }

 private:
  const char *name_ = "";
  int outPin_ = -1, sensePin_ = -1;
  SwitchMode mode_ = SW_LATCH;
  uint16_t pulseMs_ = 300;
  bool useSense_ = false;
  bool want_ = false, sensed_ = false, lastOn_ = false;
  uint32_t pulseEndMs_ = 0, verifyAtMs_ = 0, onSinceMs_ = 0;
  uint8_t retries_ = 0;
  uint32_t senseChangedMs_ = 0;
  bool senseRaw_ = false;
  void pulse();
};

// Learn-and-replay IR for the Houghton Belaire (or any) air conditioner.
// AC remotes send the whole state each press, so each "preset" (e.g.
// "Cool 22 auto fan") is one learned code. Slot "off" is the power-off code.
class AirconIr {
 public:
  void begin(int txPin, int rxPin);
  void loop();
  bool sendPreset(int idx);   // idx -1 = off
  // Start learning. Next valid code received is saved into the slot.
  void startLearn(int idx, const char *name, uint32_t timeoutMs = 20000);
  bool learning() const { return learnIdx_ != LEARN_NONE; }
  // Last learn result: 0 = none yet, 1 = ok, -1 = timeout
  int8_t learnResult() const { return learnResult_; }
  bool hasCode(int idx) const;
  bool deletePreset(int idx);

 private:
  static constexpr int LEARN_NONE = -100;
  int learnIdx_ = LEARN_NONE;
  char learnName_[16] = {};
  uint32_t learnUntilMs_ = 0;
  int8_t learnResult_ = 0;
  static String path(int idx);
};

}  // namespace hub
