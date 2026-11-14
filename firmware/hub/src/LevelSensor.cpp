#include "LevelSensor.h"

#include <NimBLEDevice.h>
#include <Wire.h>

#include "Ble.h"
#include "HubConfig.h"
#include "Leveling.h"

namespace hub {

LevelSensor gLevel;

// Bluetooth service used by web/level.html (Web Bluetooth). Keep in sync.
static const char *LEVEL_SVC = "8d5c0001-6b0e-4e4f-9c6a-2f1c7a4e0001";
static const char *LEVEL_DATA = "8d5c0002-6b0e-4e4f-9c6a-2f1c7a4e0001";  // read/notify
static const char *LEVEL_CMD = "8d5c0003-6b0e-4e4f-9c6a-2f1c7a4e0001";   // write

namespace {
NimBLECharacteristic *gDataChr = nullptr;
// Commands written by a phone arrive on the NimBLE task; hand them to loop().
volatile uint8_t gPendingCmd = 0;  // 1 zero, 2 front, 3 reset, 4 dims
volatile uint16_t gPendingTrack = 0, gPendingJockey = 0;

class CmdCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    std::string v = c->getValue();
    if (v == "zero") gPendingCmd = 1;
    else if (v == "front") gPendingCmd = 2;
    else if (v == "reset") gPendingCmd = 3;
    else if (v.rfind("dims:", 0) == 0) {  // "dims:1900,4500"
      unsigned t = 0, j = 0;
      if (sscanf(v.c_str() + 5, "%u,%u", &t, &j) == 2) {
        gPendingTrack = t;
        gPendingJockey = j;
        gPendingCmd = 4;
      }
    }
  }
};

bool regWrite(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
}  // namespace

bool LevelSensor::begin() {
  for (uint8_t a : {(uint8_t)0x68, (uint8_t)0x69}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      addr_ = a;
      ok_ = true;
      break;
    }
  }
  if (ok_) {
    regWrite(addr_, 0x6B, 0x01);  // wake up, PLL clock
    regWrite(addr_, 0x1A, 0x05);  // DLPF ~10 Hz: ignores people walking about
    regWrite(addr_, 0x1C, 0x00);  // +/-2 g full scale
    regWrite(addr_, 0x19, 9);     // 100 Hz sample rate
  }
  Serial.printf("[level] MPU-6050 %s\n", ok_ ? "found" : "NOT FOUND (levelling disabled)");
  if (ok_) startBle();
  return ok_;
}

bool LevelSensor::readAccel(float &x, float &y, float &z) {
  Wire.beginTransmission(addr_);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr_, 6) != 6) return false;
  int16_t v[3];
  for (auto &a : v) a = (int16_t)((Wire.read() << 8) | Wire.read());
  x = v[0];
  y = v[1];
  z = v[2];
  return true;
}

void LevelSensor::startZero() {
  calMode_ = CAL_ZERO;
  sx_ = sy_ = sz_ = 0;
  calN_ = 0;
  result_ = "measuring";
}

void LevelSensor::startLearnFront() {
  if (!gConfig.level.zeroed) {
    result_ = "set level first";
    return;
  }
  calMode_ = CAL_FRONT;
  sx_ = sy_ = sz_ = 0;
  calN_ = 0;
  result_ = "measuring";
}

void LevelSensor::resetCalibration() {
  gConfig.level.cal = cv::LevelCal();
  gConfig.level.zeroed = false;
  gConfig.save();
  result_ = "reset";
}

void LevelSensor::setDims(uint16_t trackMm, uint16_t jockeyMm) {
  if (trackMm >= 500 && trackMm <= 4000) gConfig.level.trackMm = trackMm;
  if (jockeyMm >= 500 && jockeyMm <= 15000) gConfig.level.jockeyMm = jockeyMm;
  gConfig.save();
}

void LevelSensor::finishCal() {
  cv::Vec3 avg{(float)(sx_ / calN_), (float)(sy_ / calN_), (float)(sz_ / calN_)};
  cv::LevelCal &cal = gConfig.level.cal;
  if (calMode_ == CAL_ZERO) {
    cv::levelSetZero(cal, avg);
    gConfig.level.zeroed = true;
    result_ = "level saved";
  } else {
    result_ = cv::levelLearnFront(cal, avg) ? "front saved" : "lift the front more (1-3 deg)";
  }
  calMode_ = CAL_NONE;
  gConfig.save();
  Serial.printf("[level] %s\n", result_);
}

void LevelSensor::loop(cv::State &s) {
  uint32_t now = millis();
  // Commands from the phone (Bluetooth task) are run here, on the main loop.
  uint8_t cmd = gPendingCmd;
  if (cmd) {
    gPendingCmd = 0;
    if (cmd == 1) startZero();
    else if (cmd == 2) startLearnFront();
    else if (cmd == 3) resetCalibration();
    else if (cmd == 4) setDims(gPendingTrack, gPendingJockey);
  }

  if (!ok_) {
    s.lvl.online = false;
    if (now - lastRetryMs_ > 30000) {  // sensor plugged in later / loose wire
      lastRetryMs_ = now;
      if (begin()) primed_ = false;
    }
    return;
  }
  if (now - lastSampleMs_ < 10) return;
  lastSampleMs_ = now;

  float x, y, z;
  if (!readAccel(x, y, z)) {
    ok_ = false;
    return;
  }
  if (calMode_ != CAL_NONE) {  // 2 s average for calibration
    sx_ += x;
    sy_ += y;
    sz_ += z;
    if (++calN_ >= 200) finishCal();
  }
  // ~0.5 s time constant: steady reading while someone walks inside.
  if (!primed_) {
    fx_ = x;
    fy_ = y;
    fz_ = z;
    primed_ = true;
  } else {
    fx_ += (x - fx_) * 0.02f;
    fy_ += (y - fy_) * 0.02f;
    fz_ += (z - fz_) * 0.02f;
  }

  const LevelCfg &lc = gConfig.level;
  cv::LevelAngles a = cv::levelCompute(lc.cal, cv::Vec3{fx_, fy_, fz_});
  s.lvl.online = true;
  s.lvl.zeroed = lc.zeroed;
  s.lvl.fwdLearned = lc.cal.fwdLearned;
  s.lvl.pitch = a.pitchDeg;
  s.lvl.roll = a.rollDeg;
  s.lvl.trackMm = lc.trackMm;
  s.lvl.jockeyMm = lc.jockeyMm;

  if (now - lastBleMs_ >= 200) {  // 5 Hz to the phone
    lastBleMs_ = now;
    notifyBle(s.lvl);
  }
}

// ---------------------------------------------------------------- Bluetooth
void LevelSensor::startBle() {
  if (gDataChr) return;
  ble::begin();
  NimBLEServer *srv = NimBLEDevice::createServer();
  srv->advertiseOnDisconnect(true);
  NimBLEService *svc = srv->createService(LEVEL_SVC);
  gDataChr = svc->createCharacteristic(LEVEL_DATA, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic *cmd = svc->createCharacteristic(LEVEL_CMD, NIMBLE_PROPERTY::WRITE);
  cmd->setCallbacks(new CmdCb());
  svc->start();
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(LEVEL_SVC);
  adv->setScanResponse(true);  // device name goes in the scan response
  adv->start();
  Serial.println("[level] Bluetooth level service advertising");
}

// 9-byte packet (fits the default 20-byte BLE payload):
//   int16 pitch*100, int16 roll*100, uint8 flags (1 online, 2 zeroed, 4 front
//   learned, 8 calibrating), uint16 track mm, uint16 jockey mm - little-endian.
void LevelSensor::notifyBle(const cv::Level &l) {
  if (!gDataChr) return;
  int16_t p = (int16_t)lroundf(constrain(l.pitch, -300.0f, 300.0f) * 100);
  int16_t r = (int16_t)lroundf(constrain(l.roll, -300.0f, 300.0f) * 100);
  uint8_t flags = (l.online ? 1 : 0) | (l.zeroed ? 2 : 0) | (l.fwdLearned ? 4 : 0) | (calMode_ != CAL_NONE ? 8 : 0);
  uint8_t b[9] = {(uint8_t)p, (uint8_t)(p >> 8), (uint8_t)r, (uint8_t)(r >> 8), flags,
                  (uint8_t)l.trackMm, (uint8_t)(l.trackMm >> 8), (uint8_t)l.jockeyMm, (uint8_t)(l.jockeyMm >> 8)};
  gDataChr->setValue(b, sizeof(b));
  gDataChr->notify();
}

}  // namespace hub
