// Mains (AC) charger detection by elimination.
//
// The two 60 A mains chargers have no sensors of their own. Every other
// charge source is measured somewhere:
//   * Victron solar controllers  -> s.solar[].batA   (summed into bat.solarA)
//   * BMPRO (its solar input + DC-DC from the vehicle) -> bat.chargerA
//   * the battery itself (BC300 shunt via the BMPRO, or the INA226 shunt) -> bat.a
// So whatever current goes into the battery (plus whatever the loads draw)
// that those sources don't explain must be coming from the mains chargers:
//
//   AC = battery net current + load current - BMPRO charger - Victron solar
//
// The load current is only known if something measures it (the BMPRO reports
// its 12 V outputs once that frame is decoded). Without it the loads are taken
// as 0, which makes the figure a floor: real AC output is AC + loads.
//
// The tile is only meant to appear during bulk charging, so the raw figure is
// smoothed and needs to stay above a threshold for a while before it shows,
// and below a lower one for a while before it hides (no flicker when a pump
// or the aircon kicks in, or a cloud passes over the panels).
#pragma once
#include <cstdint>

#include "CaravanModel.h"

namespace cv {

struct AcChargeConfig {
  float onA = 10;          // smoothed AC current to count as bulk charging
  float offA = 4;          // below this it's float / unplugged
  uint32_t onMs = 20000;   // must stay above onA this long to show
  uint32_t offMs = 60000;  // must stay below offA this long to hide
  float tauSec = 8;        // smoothing time constant
};

class AcChargeDetector {
 public:
  explicit AcChargeDetector(const AcChargeConfig &c = AcChargeConfig()) : cfg_(c) {}
  // Call after the battery, Victron and BMPRO values are in the state.
  // measuredLoadA < 0 means "no load measurement". Writes bat.acA, bat.acOn,
  // and recomputes bat.chargeA / bat.loadA to include the mains chargers.
  void update(State &s, uint32_t nowMs, float measuredLoadA = -1);
  float rawA() const { return raw_; }
  float smoothA() const { return smooth_; }
  void reset();

 private:
  AcChargeConfig cfg_;
  bool init_ = false, on_ = false;
  float raw_ = 0, smooth_ = 0;
  uint32_t lastMs_ = 0, edgeMs_ = 0;  // edgeMs_: when the pending change started (0 = none)
};

}  // namespace cv
