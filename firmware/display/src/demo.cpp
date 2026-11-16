#include "demo.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "CaravanModel.h"
#include "app.h"

namespace demo {
namespace {
uint32_t lastUpdateMs = 0;
float phase = 0;
}

void begin() {
  using namespace cv;
  State &s = app::state;
  s = {};
  s.ts = 1794738720;  // 15 Nov 2026, for a plausible demo clock.
  s.tzOff = 11 * 3600;
  s.bat.online = true;
  s.bat.source = BAT_SRC_SHUNT;
  s.bat.v = 13.31f;
  s.bat.soc = 78;
  s.bat.solarA = 31.4f;
  s.bat.chargeA = 31.4f;
  s.bat.loadA = 12.6f;
  s.bat.a = 18.8f;
  s.bat.ttgMin = 52;
  s.solar[0].online = true;
  s.solar[0].pvV = -1;
  s.solar[0].pvW = 312;
  s.solar[0].batV = 13.31f;
  s.solar[0].batA = 22.6f;
  s.solar[0].cs = 3;
  s.solar[0].yieldTodayKwh = 1.42f;
  s.solar[0].maxWToday = 498;
  s.solar[0].yieldTotalKwh = 812.4f;
  s.solar[1].online = true;
  s.solar[1].pvV = -1;
  s.solar[1].pvW = 118;
  s.solar[1].batV = 13.30f;
  s.solar[1].batA = 8.8f;
  s.solar[1].cs = 4;
  s.solar[1].yieldTodayKwh = 0.61f;
  s.solar[1].maxWToday = 205;
  s.solar[1].yieldTotalKwh = 233.9f;
  s.temp[TEMP_INSIDE].online = true;
  s.temp[TEMP_INSIDE].c = 24.6f;
  s.temp[TEMP_INSIDE].minC = 16.2f;
  s.temp[TEMP_INSIDE].maxC = 27.9f;
  s.temp[TEMP_INSIDE].batt = 86;
  s.temp[TEMP_OUTSIDE].online = true;
  s.temp[TEMP_OUTSIDE].c = 31.2f;
  s.temp[TEMP_OUTSIDE].minC = 14.8f;
  s.temp[TEMP_OUTSIDE].maxC = 33.4f;
  s.temp[TEMP_OUTSIDE].batt = 72;
  s.temp[TEMP_FRIDGE].online = true;
  s.temp[TEMP_FRIDGE].c = 3.4f;
  s.temp[TEMP_FRIDGE].minC = 2.1f;
  s.temp[TEMP_FRIDGE].maxC = 5.2f;
  s.temp[TEMP_FRIDGE].batt = 64;
  s.tank[0].online = s.tank[1].online = s.tank[2].online = true;
  s.tank[0].pct = 72;
  s.tank[1].pct = 18;
  s.tank[2].pct = 41;
  s.gas.online = true;
  s.gas.pct = 63;
  s.pump[0].on = true;
  s.pump[0].sensed = true;
  s.pump[0].runSec = 94;
  s.ac.on = true;
  s.ac.preset = 0;
  s.wifi = s.cloud = true;
  strcpy(s.ip, "192.168.1.42");
  s.lvl.online = s.lvl.zeroed = s.lvl.fwdLearned = true;
  s.lvl.roll = -0.9f;
  s.lvl.pitch = 0.6f;

  const char *wxJson = R"({"current":{"temperature_2m":27.4,"weather_code":2,"wind_speed_10m":14.2,
    "wind_gusts_10m":31.0,"is_day":1},"daily":{"time":["2026-11-15","2026-11-16","2026-11-17","2026-11-18"],
    "weather_code":[2,61,95,0],"temperature_2m_max":[29.1,24.0,22.3,26.8],"temperature_2m_min":[14.2,15.1,12.9,10.4],
    "precipitation_probability_max":[10,80,95,0],"precipitation_sum":[0.0,6.2,18.4,0.0],
    "wind_gusts_10m_max":[38.2,45.0,72.4,25.9]}})";
  parseOpenMeteo(wxJson, strlen(wxJson), s.wx);
  s.wx.updated = s.ts - 12 * 60;
  strcpy(s.wx.place, "Mudgee NSW");

  Meta &m = app::meta;
  m = {};
  strcpy(m.locName, "Mudgee NSW");
  m.locSrc = 3;
  m.acPresetCount = 5;
  const char *presets[] = {"Cool 22 auto", "Cool 24 low", "Heat 21", "Dry", "Fan only"};
  for (int i = 0; i < m.acPresetCount; i++) strcpy(m.acPresetName[i], presets[i]);
  loadDefaultRules(app::rules);

  // One sample warning makes the alert list and home banner visible in demo mode.
  s.alertCount = 1;
  s.alerts[0] = {};
  s.alerts[0].ruleId = 12;
  s.alerts[0].sev = SEV_WARN;
  s.alerts[0].since = s.ts - 3600;
  strcpy(s.alerts[0].msg, "Tank 2 low: 18% (< 20)");

  app::metaVer++;
  app::rulesVer++;
  app::linkUp = true;
  app::stateDirty = true;
  lastUpdateMs = millis();
}

void loop() {
  const uint32_t now = millis();
  if (now - lastUpdateMs < 1000) return;
  const uint32_t elapsed = (now - lastUpdateMs) / 1000;
  lastUpdateMs += elapsed * 1000;
  phase += elapsed * 0.18f;

  cv::State &s = app::state;
  s.ts += elapsed;
  s.solar[0].pvW = 312 + 45 * sinf(phase);
  s.solar[1].pvW = 118 + 20 * sinf(phase * 0.7f);
  s.bat.solarA = (s.solar[0].pvW + s.solar[1].pvW) / s.bat.v;
  // Mains chargers "plugged in" for 1 minute of every 2.5, so the Home
  // screen's Mains row can be seen appearing and going away.
  static uint32_t demoSec = 0;
  demoSec += elapsed;
  s.bat.acOn = demoSec % 150 < 60;
  s.bat.acA = s.bat.acOn ? 116 + 4 * sinf(phase * 1.3f) : 0;
  s.bat.chargeA = s.bat.solarA + s.bat.chargerA + s.bat.acA;
  s.bat.a = s.bat.chargeA - s.bat.loadA;
  s.pump[0].runSec += s.pump[0].on ? elapsed : 0;
  app::stateDirty = true;
}

void command(const JsonDocument &cmd) {
  cv::State &s = app::state;
  const char *c = cmd["c"] | "";
  if (!strcmp(c, "ack")) {
    for (int i = 0; i < s.alertCount; i++) s.alerts[i].acked = true;
  } else if (!strcmp(c, "pump")) {
    int i = cmd["i"] | -1;
    if (i < 0 || i >= cv::NUM_PUMPS) return;
    s.pump[i].on = s.pump[i].sensed = cmd["on"] | false;
    s.pump[i].runSec = 0;
  } else if (!strcmp(c, "inverter")) {
    s.inv.on = cmd["on"] | false;
  } else if (!strcmp(c, "aircon")) {
    int p = (cmd["off"] | false) ? -1 : (int)(cmd["preset"] | -1);
    s.ac.on = p >= 0;
    s.ac.preset = p;
  } else {
    return;  // settings, rules etc. aren't simulated
  }
  app::stateDirty = true;
}

}  // namespace demo
