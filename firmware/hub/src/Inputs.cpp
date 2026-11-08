#include "Inputs.h"

#include <Adafruit_ADS1X15.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Preferences.h>
#include <Wire.h>

#include "HubConfig.h"

namespace hub {

// ================================================================ Victron
void VictronPort::begin(HardwareSerial &ser, int rxPin, int txPin) {
  ser_ = &ser;
  ser_->setRxBufferSize(1024);
  ser_->begin(19200, SERIAL_8N1, rxPin, txPin);
}

void VictronPort::poll(cv::Solar &out) {
  if (!ser_) return;
  while (ser_->available()) {
    if (parser_.feed((uint8_t)ser_->read())) {
      parser_.applyTo(out);
      lastGoodMs_ = millis();
    }
  }
  // MPPTs send a block every second; 5 s of silence = offline.
  out.online = lastGoodMs_ != 0 && millis() - lastGoodMs_ < 5000;
}

// ================================================================ DS18B20
static OneWire *gOw = nullptr;
static DallasTemperature *gDallas = nullptr;

static void addrToHex(const DeviceAddress a, char out[17]) {
  for (int i = 0; i < 8; i++) sprintf(out + i * 2, "%02X", a[i]);
  out[16] = 0;
}

static bool hexToAddr(const char *h, DeviceAddress a) {
  if (strlen(h) != 16) return false;
  for (int i = 0; i < 8; i++) {
    char b[3] = {h[i * 2], h[i * 2 + 1], 0};
    a[i] = (uint8_t)strtoul(b, nullptr, 16);
  }
  return true;
}

void TempSensors::begin(int pin) {
  gOw = new OneWire(pin);
  gDallas = new DallasTemperature(gOw);
  gDallas->begin();
  gDallas->setResolution(11);
  gDallas->setWaitForConversion(false);
  Serial.printf("[temp] %d DS18B20 sensor(s) found\n", gDallas->getDeviceCount());
  autoAssign();
}

// Sensors without a saved ROM address get the first unassigned sensor found,
// so a new install works immediately; re-assign roles from the web UI.
void TempSensors::autoAssign() {
  bool changed = false;
  int n = gDallas->getDeviceCount();
  for (int role = 0; role < cv::NUM_TEMPS; role++) {
    if (gConfig.tempAddr[role][0]) continue;
    for (int i = 0; i < n; i++) {
      DeviceAddress a;
      if (!gDallas->getAddress(a, i)) continue;
      char hex[17];
      addrToHex(a, hex);
      bool used = false;
      for (int r = 0; r < cv::NUM_TEMPS; r++) used |= strcmp(gConfig.tempAddr[r], hex) == 0;
      if (!used) {
        strcpy(gConfig.tempAddr[role], hex);
        Serial.printf("[temp] auto-assigned %s -> %s\n", hex, gConfig.meta.tempName[role]);
        changed = true;
        break;
      }
    }
  }
  if (changed) gConfig.save();
}

void TempSensors::poll(cv::State &s) {
  uint32_t now = millis();
  if (!converting_) {
    if (now - requestedMs_ >= 2000) {
      gDallas->requestTemperatures();
      requestedMs_ = now;
      converting_ = true;
    }
    return;
  }
  if (now - requestedMs_ < 450) return;  // 11-bit conversion = 375 ms
  converting_ = false;

  for (int role = 0; role < cv::NUM_TEMPS; role++) {
    cv::Temp &t = s.temp[role];
    DeviceAddress a;
    if (!hexToAddr(gConfig.tempAddr[role], a)) {
      t.online = false;
      continue;
    }
    float c = gDallas->getTempC(a);
    // -127 = disconnected, 85 = power-on reset value (bad read)
    if (c <= DEVICE_DISCONNECTED_C + 1 || c == 85.0f) {
      t.online = false;
      continue;
    }
    if (!t.online && t.minC == 0 && t.maxC == 0) t.minC = t.maxC = c;
    t.online = true;
    t.c = c;
    if (c < t.minC) t.minC = c;
    if (c > t.maxC) t.maxC = c;
  }
  // Re-scan the bus occasionally so hot-plugged sensors get picked up.
  if (now - lastScanMs_ > 60000) {
    lastScanMs_ = now;
    gDallas->begin();
    gDallas->setResolution(11);
    gDallas->setWaitForConversion(false);
    autoAssign();
  }
}

void TempSensors::resetMinMax(cv::State &s) {
  for (auto &t : s.temp) t.minC = t.maxC = t.c;
}

int TempSensors::found(FoundSensor *out, int max) {
  int n = gDallas->getDeviceCount(), k = 0;
  for (int i = 0; i < n && k < max; i++) {
    DeviceAddress a;
    if (!gDallas->getAddress(a, i)) continue;
    FoundSensor &f = out[k++];
    addrToHex(a, f.addr);
    f.c = gDallas->getTempC(a);
    f.role = -1;
    for (int r = 0; r < cv::NUM_TEMPS; r++)
      if (strcmp(gConfig.tempAddr[r], f.addr) == 0) f.role = r;
  }
  return k;
}

// ================================================================ Tanks
// The BMPRO keeps reading the senders; we only TAP the sender voltage through a
// high-impedance divider (100k/33k), so the original Trek gauges are unaffected.
static Adafruit_ADS1115 gAds;

bool TankInputs::begin() {
  ok_ = gAds.begin(0x48, &Wire);
  if (ok_) {
    gAds.setGain(GAIN_ONE);  // +/-4.096 V full scale
    gAds.setDataRate(RATE_ADS1115_128SPS);
  }
  Serial.printf("[tank] ADS1115 %s\n", ok_ ? "ok" : "NOT FOUND");
  return ok_;
}

void TankInputs::poll(cv::State &s) {
  if (!ok_) {
    for (auto &t : s.tank) t.online = false;
    return;
  }
  if (millis() - lastMs_ < 250) return;  // one channel every 250 ms
  lastMs_ = millis();
  int i = next_;
  next_ = (next_ + 1) % cv::NUM_TANKS;
  const TankCal &cal = gConfig.tanks[i];
  if (!cal.enabled) {
    s.tank[i].online = false;
    return;
  }
  int16_t raw = gAds.readADC_SingleEnded(cal.channel);
  float mv = gAds.computeVolts(raw) * 1000.0f;
  rawMv_[i] = mv;
  // Heavy low-pass: water sloshes when driving.
  if (!primed_[i]) {
    filt_[i] = mv;
    primed_[i] = true;
  } else {
    filt_[i] += (mv - filt_[i]) * 0.05f;
  }
  float span = cal.fullMv - cal.emptyMv;
  if (fabsf(span) < 50) {
    s.tank[i].online = false;  // uncalibrated
    return;
  }
  float pct = (filt_[i] - cal.emptyMv) / span * 100.0f;
  s.tank[i].pct = constrain(pct, 0.0f, 100.0f);
  // Sender wire open/short detection: way outside calibrated span.
  float lo = min(cal.emptyMv, cal.fullMv) - fabsf(span) * 0.3f;
  float hi = max(cal.emptyMv, cal.fullMv) + fabsf(span) * 0.3f;
  s.tank[i].online = filt_[i] > lo && filt_[i] < hi;
}

// ================================================================ INA226 shunt
namespace {
constexpr uint8_t INA_BAT = 0x40, INA_CHG = 0x41;

bool inaWrite(uint8_t addr, uint8_t reg, uint16_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(v >> 8);
  Wire.write(v & 0xFF);
  return Wire.endTransmission() == 0;
}

bool inaRead(uint8_t addr, uint8_t reg, int16_t &out) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, 2) != 2) return false;
  out = (int16_t)((Wire.read() << 8) | Wire.read());
  return true;
}

bool inaInit(uint8_t addr) {
  // AVG=16, VBUSCT=1.1ms, VSHCT=1.1ms, continuous shunt+bus
  return inaWrite(addr, 0x00, 0x4527);
}

// Shunt voltage LSB 2.5 uV, bus voltage LSB 1.25 mV
bool inaMeasure(uint8_t addr, float shuntOhm, float &v, float &a) {
  int16_t sh, bus;
  if (!inaRead(addr, 0x01, sh) || !inaRead(addr, 0x02, bus)) return false;
  a = (sh * 2.5e-6f) / shuntOhm;
  v = (uint16_t)bus * 1.25e-3f;
  return true;
}
}  // namespace

bool BatteryShunt::begin() {
  ok_ = inaInit(INA_BAT);
  chgOk_ = gConfig.bat.chargerShunt && inaInit(INA_CHG);
  Serial.printf("[bat] INA226 battery %s, charger %s\n", ok_ ? "ok" : "NOT FOUND",
                gConfig.bat.chargerShunt ? (chgOk_ ? "ok" : "NOT FOUND") : "disabled");

  cv::SocEstimator::Config c;
  c.capacityAh = gConfig.bat.capacityAh;
  c.chargeEff = gConfig.bat.chargeEff;
  c.fullV = gConfig.bat.fullV;
  c.tailA = gConfig.bat.tailA;

  // Restore last SoC from NVS; otherwise estimate from resting voltage.
  Preferences p;
  p.begin("bat", true);
  float saved = p.getFloat("soc", -1);
  p.end();
  float initial = saved;
  if (initial < 0) {
    float v = 0, a = 0;
    initial = (ok_ && inaMeasure(INA_BAT, gConfig.bat.shuntOhm, v, a))
                  ? cv::SocEstimator::socFromRestVoltageLfp(v * gConfig.bat.vCal)
                  : 50;
  }
  soc_.begin(c, initial);
  lastSavedSoc_ = initial;
  return ok_;
}

void BatteryShunt::poll(cv::State &s, uint32_t nowMs) {
  if (nowMs - lastMs_ < 200) return;
  lastMs_ = nowMs;

  // Charge sources we can see directly
  float solarA = 0;
  for (auto &x : s.solar)
    if (x.online) solarA += x.batA;
  s.bat.solarA = solarA;

  if (chgOk_) {
    float cv_, ca;
    if (inaMeasure(INA_CHG, gConfig.bat.chargerShuntOhm, cv_, ca)) s.bat.chargerA = max(0.0f, ca);
  }
  s.bat.chargeA = s.bat.solarA + s.bat.chargerA;

  if (gConfig.bat.source != cv::BAT_SRC_SHUNT) return;  // BMPRO driver fills these
  float v, a;
  if (!ok_ || !inaMeasure(INA_BAT, gConfig.bat.shuntOhm, v, a)) {
    s.bat.online = false;
    if (!ok_ && nowMs % 10000 < 200) ok_ = inaInit(INA_BAT);  // retry occasionally
    return;
  }
  v *= gConfig.bat.vCal;
  soc_.update(v, a, nowMs);
  avgA_ += (a - avgA_) * 0.02f;  // ~10 s average for time-to-go

  s.bat.online = true;
  s.bat.source = cv::BAT_SRC_SHUNT;
  s.bat.v = v;
  s.bat.a = a;
  s.bat.soc = soc_.soc();
  s.bat.ttgMin = soc_.timeToGoMin(avgA_);
  // Everything flowing in that isn't going into the battery is feeding loads.
  s.bat.loadA = max(0.0f, s.bat.chargeA - a);
  persist();
}

void BatteryShunt::persist(bool force) {
  uint32_t now = millis();
  float soc = soc_.soc();
  // Save at most every 5 minutes and only on >0.5% change (flash wear).
  if (!force && (now - lastSaveMs_ < 300000 || fabsf(soc - lastSavedSoc_) < 0.5f)) return;
  lastSaveMs_ = now;
  lastSavedSoc_ = soc;
  Preferences p;
  p.begin("bat", false);
  p.putFloat("soc", soc);
  p.end();
}

}  // namespace hub
