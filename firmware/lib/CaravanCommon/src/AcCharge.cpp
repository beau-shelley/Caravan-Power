#include "AcCharge.h"

#include <cmath>

namespace cv {

void AcChargeDetector::reset() {
  init_ = on_ = false;
  raw_ = smooth_ = 0;
  edgeMs_ = 0;
}

void AcChargeDetector::update(State &s, uint32_t nowMs, float measuredLoadA) {
  Battery &b = s.bat;
  float known = b.solarA + b.chargerA;
  bool haveLoad = measuredLoadA >= 0;
  if (!b.online) {
    reset();
    b.acA = 0;
    b.acOn = false;
    b.chargeA = known;
    b.loadA = haveLoad ? measuredLoadA : (known - b.a > 0 ? known - b.a : 0);
    return;
  }

  raw_ = b.a + (haveLoad ? measuredLoadA : 0) - known;
  if (!init_) {
    init_ = true;
    smooth_ = raw_;
  } else {
    float dt = (nowMs - lastMs_) / 1000.0f;
    float k = dt / (cfg_.tauSec + dt);
    smooth_ += (raw_ - smooth_) * k;
  }
  lastMs_ = nowMs;

  // Debounce: the pending edge must hold for onMs / offMs.
  bool want = on_ ? smooth_ >= cfg_.offA : smooth_ >= cfg_.onA;
  if (want == on_) {
    edgeMs_ = 0;
  } else {
    if (!edgeMs_) edgeMs_ = nowMs ? nowMs : 1;
    if (nowMs - edgeMs_ >= (on_ ? cfg_.offMs : cfg_.onMs)) {
      on_ = want;
      edgeMs_ = 0;
    }
  }

  b.acOn = on_;
  b.acA = on_ && smooth_ > 0 ? smooth_ : 0;
  b.chargeA = known + b.acA;
  if (haveLoad) b.loadA = measuredLoadA;
  else b.loadA = b.chargeA - b.a > 0 ? b.chargeA - b.a : 0;
}

}  // namespace cv
