#include "AirHeater.h"

namespace cv {
namespace airheater {

void encode(uint8_t cmd, uint16_t arg, uint16_t passkey, uint8_t out[CMD_LEN]) {
  out[0] = 0xAA;
  out[1] = 0x55;
  out[2] = (uint8_t)(passkey / 100);
  out[3] = (uint8_t)(passkey % 100);
  out[4] = cmd;
  out[5] = (uint8_t)(arg & 0xFF);
  out[6] = (uint8_t)(arg >> 8);
  uint8_t sum = 0;
  for (int i = 2; i < 7; i++) sum += out[i];
  out[7] = sum;
}

int framesFor(const HeaterCmd &c, const Heater &cur, uint16_t passkey, uint8_t out[][CMD_LEN], int maxFrames) {
  int n = 0;
  uint8_t mode = c.mode >= 0 ? (uint8_t)c.mode : cur.mode;
  if (c.mode >= 0 && n < maxFrames)
    encode(CMD_MODE, mode == HTR_MODE_LEVEL ? WIRE_LEVEL : WIRE_TEMP, passkey, out[n++]);
  // A set point for the other mode is still sent: it switches mode first.
  if (c.temp >= 0 && n < maxFrames) {
    if (mode != HTR_MODE_TEMP && c.mode < 0 && n < maxFrames) encode(CMD_MODE, WIRE_TEMP, passkey, out[n++]);
    if (n < maxFrames) encode(CMD_SETPOINT, (uint16_t)c.temp, passkey, out[n++]);
  }
  if (c.level >= 0 && n < maxFrames) {
    if (mode != HTR_MODE_LEVEL && c.mode < 0 && n < maxFrames) encode(CMD_MODE, WIRE_LEVEL, passkey, out[n++]);
    if (n < maxFrames) encode(CMD_SETPOINT, (uint16_t)c.level, passkey, out[n++]);
  }
  if (c.on >= 0 && n < maxFrames) encode(CMD_POWER, c.on ? 1 : 0, passkey, out[n++]);
  return n;
}

static int16_t s16(const uint8_t *d) { return (int16_t)(d[0] | d[1] << 8); }

bool decode(const uint8_t *d, size_t len, Heater &h) {
  if (len < STATUS_MIN_LEN || d[0] != 0xAA || d[1] != 0x55) return false;
  uint8_t running = d[3], err = d[4], step = d[5], mode = d[8];
  if (step > 4 || mode > 3) return false;  // not a frame we understand
  h.err = err;
  switch (step) {
    case 1:
    case 2: h.phase = HTR_STARTING; break;
    case 3: h.phase = HTR_RUNNING; break;
    case 4: h.phase = HTR_COOLING; break;
    default: h.phase = running ? HTR_STARTING : HTR_OFF; break;
  }
  if (mode == WIRE_LEVEL) {
    h.mode = HTR_MODE_LEVEL;
    if (d[9] >= 1 && d[9] <= HEATER_LEVELS) h.level = d[9];
  } else if (mode == WIRE_TEMP) {
    h.mode = HTR_MODE_TEMP;
    if (d[9] >= HEATER_MIN_C && d[9] <= HEATER_MAX_C) h.setC = (int8_t)d[9];
    if (d[10] + 1 <= HEATER_LEVELS) h.level = d[10] + 1;
  }
  h.supplyV = (d[11] | d[12] << 8) / 10.0f;
  h.bodyC = s16(d + 13);
  h.cabinC = s16(d + 15);
  return true;
}

}  // namespace airheater

// ---------------------------------------------------------------- simulator
namespace {
constexpr float kLightSec = 40, kCoolSec = 30;
float approach(float v, float target, float rate, float dt) {
  float k = rate * dt;
  if (k > 1) k = 1;
  return v + (target - v) * k;
}
}  // namespace

bool HeaterSim::apply(Heater &h, const HeaterCmd &c, const char **why) {
  if (c.on == 1 && h.phase == HTR_COOLING) {
    if (why) *why = "Heater is still cooling down - try again in a moment";
    return false;
  }
  if (c.mode >= 0) h.mode = (uint8_t)c.mode;
  if (c.temp >= 0) {
    h.setC = c.temp;
    if (c.mode < 0) h.mode = HTR_MODE_TEMP;
  }
  if (c.level >= 0) {
    h.level = (uint8_t)c.level;
    if (c.mode < 0) h.mode = HTR_MODE_LEVEL;
  }
  if (c.on == 1 && !h.running()) {
    h.phase = HTR_STARTING;
    h.err = 0;
    phaseSec_ = 0;
  } else if (c.on == 0 && h.running()) {
    h.phase = HTR_COOLING;
    phaseSec_ = 0;
  }
  return true;
}

void HeaterSim::tick(Heater &h, float dt, float ambientC, float batV) {
  if (cabin_ < -99) cabin_ = ambientC;
  phaseSec_ += dt;
  h.supplyV = batV;
  float bodyTarget = ambientC, cabinTarget = ambientC;
  switch (h.phase) {
    case HTR_STARTING:
      bodyTarget = 90;
      if (phaseSec_ >= kLightSec) {
        h.phase = HTR_RUNNING;
        phaseSec_ = 0;
      }
      break;
    case HTR_RUNNING: {
      // Thermostat: full heat until close to the set point, then it idles
      // down. Power level: a fixed output.
      float out = h.mode == HTR_MODE_LEVEL ? h.level / 10.0f : (h.setC - cabin_ + 1) / 4;
      out = out < 0.15f ? 0.15f : out > 1 ? 1 : out;
      bodyTarget = 80 + 90 * out;
      cabinTarget = h.mode == HTR_MODE_LEVEL ? ambientC + 4 + 1.6f * h.level : h.setC + 0.4f;
      if (cabinTarget < ambientC) cabinTarget = ambientC;  // a warm day stays warm
      break;
    }
    case HTR_COOLING:
      bodyTarget = ambientC;
      if (phaseSec_ >= kCoolSec) {
        h.phase = HTR_OFF;
        phaseSec_ = 0;
      }
      break;
    default: break;
  }
  if (h.bodyC == 0) h.bodyC = ambientC;
  h.bodyC = approach(h.bodyC, bodyTarget, h.phase == HTR_COOLING ? 0.12f : 0.08f, dt);
  // The cabin warms slowly while the heater runs and drifts back after.
  cabin_ = approach(cabin_, cabinTarget, h.phase == HTR_RUNNING ? 0.02f : 0.008f, dt);
  h.cabinC = cabin_;
}

float HeaterSim::currentA(const Heater &h) const {
  switch (h.phase) {
    case HTR_STARTING: return phaseSec_ < kLightSec * 0.6f ? 8.5f : 2.5f;  // glow plug, then fan + pump
    case HTR_RUNNING: return 0.9f + 0.18f * (h.mode == HTR_MODE_LEVEL ? h.level : 5);
    case HTR_COOLING: return 1.6f;  // fan only
    default: return 0;
  }
}

}  // namespace cv
