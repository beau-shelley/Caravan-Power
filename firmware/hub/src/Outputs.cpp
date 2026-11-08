#include "Outputs.h"

#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>
#include <LittleFS.h>

namespace hub {

// ================================================================ SwitchedOutput
void SwitchedOutput::begin(const char *name, int outPin, int sensePin) {
  name_ = name;
  outPin_ = outPin;
  sensePin_ = sensePin;
  pinMode(outPin_, OUTPUT);
  digitalWrite(outPin_, LOW);
  if (sensePin_ >= 0) pinMode(sensePin_, INPUT_PULLUP);
}

void SwitchedOutput::configure(SwitchMode mode, uint16_t pulseMs, bool useSense) {
  if (mode != mode_) digitalWrite(outPin_, LOW);
  mode_ = mode;
  pulseMs_ = pulseMs;
  useSense_ = useSense;
  if (mode_ == SW_DISABLED) want_ = false;
}

void SwitchedOutput::pulse() {
  digitalWrite(outPin_, HIGH);
  pulseEndMs_ = millis() + pulseMs_;
  verifyAtMs_ = pulseEndMs_ + 1500;  // give the device time to react
}

bool SwitchedOutput::set(bool on, const char **reason) {
  if (mode_ == SW_DISABLED) {
    if (reason) *reason = "output disabled in settings";
    return false;
  }
  want_ = on;
  retries_ = 0;
  if (mode_ == SW_LATCH) {
    digitalWrite(outPin_, on ? HIGH : LOW);
  } else {
    // In pulse mode only toggle if the device isn't already in that state.
    if (isOn() != on || !hasSense()) pulse();
  }
  Serial.printf("[out] %s -> %s\n", name_, on ? "ON" : "OFF");
  return true;
}

void SwitchedOutput::loop() {
  uint32_t now = millis();
  // Debounced sense input (opto pulls LOW when the device has power).
  if (sensePin_ >= 0) {
    bool raw = digitalRead(sensePin_) == LOW;
    if (raw != senseRaw_) {
      senseRaw_ = raw;
      senseChangedMs_ = now;
    } else if (now - senseChangedMs_ > 150) {
      sensed_ = raw;
    }
  }
  if (mode_ == SW_PULSE) {
    if (pulseEndMs_ && (int32_t)(now - pulseEndMs_) >= 0) {
      digitalWrite(outPin_, LOW);
      pulseEndMs_ = 0;
    }
    // Verify the toggle took effect, retry twice, then accept reality.
    if (verifyAtMs_ && (int32_t)(now - verifyAtMs_) >= 0) {
      verifyAtMs_ = 0;
      if (hasSense() && sensed_ != want_) {
        if (retries_++ < 2) pulse();
        else want_ = sensed_;
      }
    }
    // Device changed by its own button / BMPRO: follow it.
    if (hasSense() && !pulseEndMs_ && !verifyAtMs_) want_ = sensed_;
  }
  bool on = isOn();
  if (on && !lastOn_) onSinceMs_ = now;
  lastOn_ = on;
}

// ================================================================ Aircon IR
static IRsend *gIrSend = nullptr;
static IRrecv *gIrRecv = nullptr;
static decode_results gIrResults;

String AirconIr::path(int idx) { return idx < 0 ? String("/ir/off.bin") : "/ir/p" + String(idx) + ".bin"; }

void AirconIr::begin(int txPin, int rxPin) {
  gIrSend = new IRsend(txPin);
  gIrSend->begin();
  // Large buffer + long timeout: AC remotes send long (>200 mark/space) frames.
  gIrRecv = new IRrecv(rxPin, 1024, 50, true);
  gIrRecv->setUnknownThreshold(12);
  if (!LittleFS.exists("/ir")) LittleFS.mkdir("/ir");
}

bool AirconIr::hasCode(int idx) const { return LittleFS.exists(path(idx)); }

bool AirconIr::sendPreset(int idx) {
  File f = LittleFS.open(path(idx), "r");
  if (!f) return false;
  size_t n = f.size() / sizeof(uint16_t);
  if (n == 0 || n > 1024) {
    f.close();
    return false;
  }
  uint16_t *buf = (uint16_t *)malloc(n * sizeof(uint16_t));
  if (!buf) {
    f.close();
    return false;
  }
  f.read((uint8_t *)buf, n * sizeof(uint16_t));
  f.close();
  if (learning()) gIrRecv->disableIRIn();
  gIrSend->sendRaw(buf, n, 38);
  delay(60);
  gIrSend->sendRaw(buf, n, 38);  // send twice; the unit ignores duplicates
  if (learning()) gIrRecv->enableIRIn();
  free(buf);
  Serial.printf("[ac] sent preset %d (%u timings)\n", idx, (unsigned)n);
  return true;
}

void AirconIr::startLearn(int idx, const char *name, uint32_t timeoutMs) {
  learnIdx_ = idx;
  strncpy(learnName_, name ? name : "", sizeof(learnName_) - 1);
  learnUntilMs_ = millis() + timeoutMs;
  learnResult_ = 0;
  gIrRecv->enableIRIn();
  Serial.printf("[ac] learning slot %d '%s' - point the remote at the receiver\n", idx, learnName_);
}

bool AirconIr::deletePreset(int idx) {
  bool ok = LittleFS.remove(path(idx));
  if (idx >= 0 && idx < gConfig.meta.acPresetCount) {
    // Shift later presets down so the list stays contiguous.
    for (int i = idx; i < gConfig.meta.acPresetCount - 1; i++) {
      memcpy(gConfig.meta.acPresetName[i], gConfig.meta.acPresetName[i + 1], 16);
      LittleFS.rename(path(i + 1), path(i));
    }
    gConfig.meta.acPresetCount--;
    gConfig.save();
  }
  return ok;
}

void AirconIr::loop() {
  if (!learning()) return;
  if ((int32_t)(millis() - learnUntilMs_) > 0) {
    gIrRecv->disableIRIn();
    learnIdx_ = LEARN_NONE;
    learnResult_ = -1;
    Serial.println("[ac] learn timeout");
    return;
  }
  if (!gIrRecv->decode(&gIrResults)) return;
  uint16_t len = getCorrectedRawLength(&gIrResults);
  if (len < 20) {  // noise / repeat code
    gIrRecv->resume();
    return;
  }
  uint16_t *raw = resultToRawArray(&gIrResults);
  File f = LittleFS.open(path(learnIdx_), "w");
  if (f) {
    f.write((const uint8_t *)raw, len * sizeof(uint16_t));
    f.close();
  }
  delete[] raw;
  gIrRecv->disableIRIn();
  Serial.printf("[ac] learned slot %d: %u timings, protocol %s\n", learnIdx_, len,
                typeToString(gIrResults.decode_type).c_str());

  if (learnIdx_ >= 0) {
    int idx = learnIdx_;
    if (idx >= gConfig.meta.acPresetCount) idx = gConfig.meta.acPresetCount++;
    strncpy(gConfig.meta.acPresetName[idx], learnName_[0] ? learnName_ : "Preset", 15);
    if (idx != learnIdx_) LittleFS.rename(path(learnIdx_), path(idx));
    gConfig.save();
  }
  learnIdx_ = LEARN_NONE;
  learnResult_ = 1;
}

}  // namespace hub
