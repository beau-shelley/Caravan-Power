#include "Demo.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

namespace hub {
namespace demo {
namespace {
constexpr uint32_t kFakeEpoch = 1794738720;  // 15 Nov 2026 - used until Wi-Fi sets the real clock
constexpr float kDayLenSec = 600;            // a whole "day" of sun every 10 minutes
uint32_t lastMs = 0;
float t = 0;            // demo seconds since boot
float soc = 78;
float tank[cv::NUM_TANKS] = {72, 18, 41};
float lastLoad = 0;
constexpr float kAcCycleSec = 420;  // mains chargers plugged in for the first
constexpr float kAcBulkSec = 120;   // 2 minutes of every 7 (bulk), then
constexpr float kAcPlugSec = 150;   // 30 s of float before "unplugging"

void minMax(cv::Temp &x) {
  if (x.c < x.minC) x.minC = x.c;
  if (x.c > x.maxC) x.maxC = x.c;
}
}  // namespace

void begin(cv::State &s) {
  Serial.println("[demo] DEMO MODE - sensors are simulated, nothing real is read");
  s.temp[cv::TEMP_INSIDE] = {true, 22, 16.2f, 27.9f, 86};
  s.temp[cv::TEMP_OUTSIDE] = {true, 25, 14.8f, 33.4f, 72};
  s.temp[cv::TEMP_FRIDGE] = {true, 3.4f, 2.1f, 5.2f, 64};
  s.solar[0].yieldTotalKwh = 812.4f;
  s.solar[1].yieldTotalKwh = 233.9f;
  lastMs = millis();
}

void tick(cv::State &s, uint32_t nowMs) {
  // Before Wi-Fi/NTP sets the clock, keep a plausible one ticking.
  if (!s.ts) {
    s.ts = kFakeEpoch + nowMs / 1000;
    s.tzOff = 11 * 3600;
  }
  if (nowMs - lastMs < 1000) return;
  float dt = (nowMs - lastMs) / 1000.0f;
  lastMs = nowMs;
  t += dt;

  // ---- sun: 9 minutes of "day" peaking at ~660 W across both panels, then a
  // 1-minute "night" with no solar (short, so the bench test rarely looks dead)
  float frac = fmodf(t, kDayLenSec) / kDayLenSec;
  float sun = frac < 0.9f ? sinf(PI * frac / 0.9f) : 0;
  if (sun < 0.15f && frac < 0.9f) sun = 0.15f;  // dawn/dusk still trickle-charges
  float cloud = 0.85f + 0.15f * sinf(t * 0.37f);  // passing clouds
  // Roof: 3 x 230 W = 690 W; portable: 880 W. Real panels peak around 78% of
  // their rating at midday (heat, angle, wiring losses).
  float w1 = 690 * 0.78f * sun * cloud, w2 = 880 * 0.78f * sun * cloud;

  // ---- loads: base fridge/lights plus whatever is switched on
  float load = 4.5f + 1.5f * sinf(t * 0.21f);
  if (s.pump[0].on) load += 6;
  if (s.pump[1].on) load += 6;
  if (s.ac.on) load += 45;
  if (s.inv.on) load += 3;

  // ---- mains: the two 60 A chargers, plugged in for part of each cycle.
  // Nothing on the hub measures them; it works them out from what's left over.
  float acT = fmodf(t, kAcCycleSec);
  float ac = acT < kAcBulkSec   ? 116 + 4 * sinf(t * 0.5f)  // bulk: 2 x 60 A, ~120 A peak
             : acT < kAcPlugSec ? 2.5f                       // float trickle
                                : 0;

  // ---- BMPRO: DC-DC from the car for a stretch mid-"day" (driving)
  float frac2 = fmodf(t, kDayLenSec) / kDayLenSec;
  float bmpro = frac2 > 0.55f && frac2 < 0.7f ? 18 + 2 * sinf(t * 0.3f) : 0;

  float batV = 12.6f + soc * 0.008f + (ac > 10 ? 1.4f : 0);
  float a1 = w1 / batV, a2 = w2 / batV;
  float charge = a1 + a2 + bmpro + ac;
  if (soc >= 100 && charge > load) charge = load;  // full: chargers back off
  float net = charge - load;
  // 300 Ah battery, time sped up 20x (mains only 2x, or it fills in seconds)
  float socNet = net > ac ? net - ac * 0.9f : net;
  soc += socNet * dt / 3600 / 300 * 100 * 20;
  soc = soc < 5 ? 5 : soc > 100 ? 100 : soc;
  batV += net * 0.01f;

  for (int i = 0; i < cv::NUM_SOLAR; i++) {
    cv::Solar &p = s.solar[i];
    float w = i ? w2 : w1;
    p.online = true;
    p.pvW = w;
    p.pvV = w > 1 ? 36 + 4 * sun : 0;
    p.batV = batV;
    p.batA = i ? a2 : a1;
    p.cs = w < 1 ? 0 : soc < 90 ? 3 : soc < 99 ? 4 : 5;  // off / bulk / absorption / float
    p.yieldTodayKwh += w * dt / 3600000 * 20;
    p.yieldTotalKwh += w * dt / 3600000 * 20;
    if (w > p.maxWToday) p.maxWToday = w;
  }

  cv::Battery &b = s.bat;
  b.online = true;
  b.source = cv::BAT_SRC_SHUNT;
  b.v = batV;
  b.soc = soc;
  b.solarA = a1 + a2;  // Victron controllers
  b.chargerA = bmpro;  // BMPRO (solar input + DC-DC); mains left for the hub to infer
  b.chargeA = charge;
  b.loadA = load;
  lastLoad = load;
  b.a = net;
  b.ttgMin = net < -0.2f ? (int32_t)(soc / 100 * 300 / -net * 60)
             : net > 0.2f ? (int32_t)((100 - soc) / 100 * 300 / net * 60) : -1;

  // ---- temperatures follow the sun; the fridge cycles its compressor
  s.temp[cv::TEMP_OUTSIDE].c = 18 + 13 * sun;
  s.temp[cv::TEMP_INSIDE].c = s.ac.on ? 22.5f : 20 + 8 * sun;
  s.temp[cv::TEMP_FRIDGE].c = 3.5f + 1.8f * sinf(t * 2 * PI / 240);
  for (auto &x : s.temp) minMax(x);

  // ---- tanks: pump 1 drinks Tank 1 (refills when nearly empty so the
  // dry-run lockout and low-tank alerts can be seen), grey slowly fills.
  if (s.pump[0].on) tank[0] -= 0.5f * dt;
  if (s.pump[1].on) tank[1] -= 0.5f * dt;
  tank[2] += 0.02f * dt;
  for (int i = 0; i < 2; i++)
    if (tank[i] < 2) tank[i] = 85;  // "filled up at the tap"
  if (tank[2] > 95) tank[2] = 10;   // "emptied at the dump point"
  for (int i = 0; i < cv::NUM_TANKS; i++) {
    s.tank[i].online = true;
    s.tank[i].pct = tank[i];
  }

  // ---- gas bottle: slowly used by the stove/hot water, swapped when empty
  static float gas = 63;
  gas -= 0.01f * dt;
  if (gas < 3) gas = 100;
  s.gas.online = true;
  s.gas.pct = gas;

  // ---- level: a little wobble, as if someone is walking around inside
  s.lvl.online = s.lvl.zeroed = s.lvl.fwdLearned = true;
  s.lvl.roll = -0.9f + 0.3f * sinf(t * 0.9f);
  s.lvl.pitch = 0.6f + 0.2f * sinf(t * 0.6f);

  // ---- weather, if the real forecast hasn't arrived (no Wi-Fi)
  if (!s.wx.updated) {
    const char *wx = R"({"current":{"temperature_2m":27.4,"weather_code":2,"wind_speed_10m":14.2,
      "wind_gusts_10m":31.0,"is_day":1},"daily":{"time":["2026-11-15","2026-11-16","2026-11-17","2026-11-18"],
      "weather_code":[2,61,95,0],"temperature_2m_max":[29.1,24.0,22.3,26.8],"temperature_2m_min":[14.2,15.1,12.9,10.4],
      "precipitation_probability_max":[10,80,95,0],"precipitation_sum":[0.0,6.2,18.4,0.0],
      "wind_gusts_10m_max":[38.2,45.0,72.4,25.9]}})";
    if (cv::parseOpenMeteo(wx, strlen(wx), s.wx)) {
      s.wx.valid = true;
      s.wx.updated = s.ts;
      strcpy(s.wx.place, "Mudgee NSW");
    }
  }
}

float loadA() { return lastLoad; }

}  // namespace demo
}  // namespace hub
