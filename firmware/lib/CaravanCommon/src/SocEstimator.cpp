#include "SocEstimator.h"

namespace cv {

void SocEstimator::begin(const Config &c, float initialSocPct) {
  cfg_ = c;
  setSoc(initialSocPct);
  haveLast_ = false;
}

void SocEstimator::setSoc(float pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  ah_ = cfg_.capacityAh * pct / 100.0;
}

float SocEstimator::soc() const {
  if (cfg_.capacityAh <= 0) return 0;
  return (float)(ah_ / cfg_.capacityAh * 100.0);
}

void SocEstimator::update(float v, float a, uint32_t nowMs) {
  if (haveLast_) {
    uint32_t dt = nowMs - lastMs_;
    if (dt > 10000) dt = 10000;  // ignore gaps (e.g. sensor dropout)
    double ah = (double)a * dt / 3600000.0;
    if (ah > 0) ah *= cfg_.chargeEff;
    ah_ += ah;
    if (ah_ < 0) ah_ = 0;
    if (ah_ > cfg_.capacityAh) ah_ = cfg_.capacityAh;
  }
  lastMs_ = nowMs;
  haveLast_ = true;

  // Full-charge synchronisation (same idea as a Victron BMV).
  bool fullCond = v >= cfg_.fullV && a >= 0 && a < cfg_.tailA;
  if (fullCond) {
    if (!fullPending_) {
      fullPending_ = true;
      fullSinceMs_ = nowMs;
    } else if (nowMs - fullSinceMs_ >= cfg_.fullHoldMs) {
      ah_ = cfg_.capacityAh;
      synced_ = true;
    }
  } else {
    fullPending_ = false;
  }
}

int32_t SocEstimator::timeToGoMin(float a) const {
  if (a < -0.3f) return (int32_t)(ah_ / -a * 60.0 + 0.5);
  if (a > 0.3f) return (int32_t)((cfg_.capacityAh - ah_) / (a * cfg_.chargeEff) * 60.0 + 0.5);
  return -1;
}

float SocEstimator::socFromRestVoltageLfp(float v) {
  // Typical 4S LiFePO4 resting curve.
  static const float pts[][2] = {{10.0f, 0},   {12.0f, 9},   {12.5f, 14},  {12.8f, 17},
                                 {12.9f, 20},  {13.0f, 30},  {13.05f, 40}, {13.1f, 50},
                                 {13.15f, 60}, {13.2f, 70},  {13.25f, 80}, {13.3f, 90},
                                 {13.4f, 99},  {13.6f, 100}};
  const int n = sizeof(pts) / sizeof(pts[0]);
  if (v <= pts[0][0]) return 0;
  if (v >= pts[n - 1][0]) return 100;
  for (int i = 1; i < n; i++) {
    if (v <= pts[i][0]) {
      float f = (v - pts[i - 1][0]) / (pts[i][0] - pts[i - 1][0]);
      return pts[i - 1][1] + f * (pts[i][1] - pts[i - 1][1]);
    }
  }
  return 100;
}

}  // namespace cv
