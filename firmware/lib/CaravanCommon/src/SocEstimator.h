// Coulomb-counting state-of-charge estimator for a battery shunt.
// Portable (no Arduino deps) so it can be unit tested on the host.
#pragma once
#include <cstdint>

namespace cv {

class SocEstimator {
 public:
  struct Config {
    float capacityAh = 300;   // usable capacity
    float chargeEff = 0.99f;  // LiFePO4 ~0.99, AGM ~0.85
    float fullV = 13.9f;      // "charged" voltage (absorption reached)...
    float tailA = 4.0f;       // ...with charge current tapering below this
    uint32_t fullHoldMs = 120000;  // ...for this long -> sync to 100%
    // Second way to know it's full: the charger has gone to float (lithium
    // float 13.6-13.8 V) and is holding the bank there with next to no
    // current going in or out. A LiFePO4 bank held this high is full even if
    // the charger skipped absorption or ended it before tailA.
    float floatV = 13.55f;
    float floatTailA = 3.0f;
    uint32_t floatHoldMs = 600000;  // 10 minutes
  };

  void begin(const Config &c, float initialSocPct);
  // volts, amps (+ = charging), monotonic ms
  void update(float v, float a, uint32_t nowMs);
  void setSoc(float pct);
  // New battery size: keeps the same %, so a correction doesn't jump the gauge.
  void setCapacityAh(float ah);
  float soc() const;           // 0..100
  float remainingAh() const { return ah_; }
  // Minutes to empty (discharging) or full (charging); -1 if ~idle.
  int32_t timeToGoMin(float a) const;
  bool synced() const { return synced_; }
  const Config &config() const { return cfg_; }

  // Rough SoC from a rested LiFePO4 pack voltage (12.8 V nominal).
  static float socFromRestVoltageLfp(float v);

 private:
  Config cfg_;
  double ah_ = 0;
  uint32_t lastMs_ = 0;
  bool haveLast_ = false;
  uint32_t fullSinceMs_ = 0, floatSinceMs_ = 0;
  bool fullPending_ = false, floatPending_ = false;
  bool synced_ = false;
};

}  // namespace cv
