#include "CaravanModel.h"

#include <cmath>
#include <cstdio>

namespace cv {

static const MetricInfo kMetrics[M_COUNT] = {
    {"bat_v", "Battery voltage", "V", 0.1f},
    {"bat_soc", "Battery charge", "%", 1.0f},
    {"bat_a", "Battery current", "A", 1.0f},
    {"load_a", "Load current", "A", 1.0f},
    {"charge_a", "Charge current", "A", 1.0f},
    {"solar_w", "Solar total", "W", 10.0f},
    {"solar1_w", "Solar 1", "W", 10.0f},
    {"solar2_w", "Solar 2", "W", 10.0f},
    {"temp_in", "Inside temp", "\xC2\xB0" "C", 0.5f},
    {"temp_out", "Outside temp", "\xC2\xB0" "C", 0.5f},
    {"temp_fridge", "Fridge temp", "\xC2\xB0" "C", 0.5f},
    {"tank1", "Tank 1", "%", 5.0f},
    {"tank2", "Tank 2", "%", 5.0f},
    {"tank3", "Tank 3", "%", 5.0f},
    {"wx_gust", "Forecast gusts", "km/h", 5.0f},
    {"wx_rain", "Forecast rain chance", "%", 10.0f},
    {"gas", "Gas bottle", "%", 5.0f},
};

const MetricInfo &metricInfo(Metric m) { return kMetrics[m < M_COUNT ? m : 0]; }

Metric metricFromKey(const char *key) {
  if (key)
    for (int i = 0; i < M_COUNT; i++)
      if (strcmp(kMetrics[i].key, key) == 0) return (Metric)i;
  return M_COUNT;
}

bool metricValue(const State &s, Metric m, float &out) {
  switch (m) {
    case M_BAT_V: out = s.bat.v; return s.bat.online;
    case M_BAT_SOC: out = s.bat.soc; return s.bat.online && s.bat.source != BAT_SRC_NONE;
    case M_BAT_A: out = s.bat.a; return s.bat.online;
    case M_LOAD_A: out = s.bat.loadA; return s.bat.online;
    case M_CHARGE_A: out = s.bat.chargeA; return s.bat.online;
    case M_SOLAR_W: {
      bool any = false;
      for (auto &x : s.solar) any |= x.online;
      out = s.solarW();
      return any;
    }
    case M_SOLAR1_W: out = s.solar[0].pvW; return s.solar[0].online;
    case M_SOLAR2_W: out = s.solar[1].pvW; return s.solar[1].online;
    case M_TEMP_IN: out = s.temp[TEMP_INSIDE].c; return s.temp[TEMP_INSIDE].online;
    case M_TEMP_OUT: out = s.temp[TEMP_OUTSIDE].c; return s.temp[TEMP_OUTSIDE].online;
    case M_TEMP_FRIDGE: out = s.temp[TEMP_FRIDGE].c; return s.temp[TEMP_FRIDGE].online;
    case M_TANK1: out = s.tank[0].pct; return s.tank[0].online;
    case M_TANK2: out = s.tank[1].pct; return s.tank[1].online;
    case M_TANK3: out = s.tank[2].pct; return s.tank[2].online;
    // Forecast metrics: only while the forecast is reasonably fresh (6 h).
    case M_WX_GUST: out = s.wx.day[0].gustMax; return s.wx.valid && s.ts && s.ts - s.wx.updated < 6 * 3600;
    case M_WX_RAIN: out = s.wx.day[0].rainProb; return s.wx.valid && s.ts && s.ts - s.wx.updated < 6 * 3600;
    case M_GAS: out = s.gas.pct; return s.gas.online;
    default: return false;
  }
}

static const char *kActions[ACT_COUNT] = {"none", "aircon_off", "inverter_off", "pumps_off"};
const char *actionKey(Action a) { return kActions[a < ACT_COUNT ? a : 0]; }
Action actionFromKey(const char *k) {
  if (k)
    for (int i = 0; i < ACT_COUNT; i++)
      if (strcmp(kActions[i], k) == 0) return (Action)i;
  return ACT_NONE;
}

const char *chargeStateName(uint8_t cs) {
  switch (cs) {
    case 0: return "Off";
    case 2: return "Fault";
    case 3: return "Bulk";
    case 4: return "Absorption";
    case 5: return "Float";
    case 7: return "Equalise";
    case 245: return "Starting";
    case 247: return "Auto EQ";
    case 252: return "Ext control";
    default: return "Unknown";
  }
}

const char *severityName(uint8_t sev) {
  return sev == SEV_ALARM ? "alarm" : sev == SEV_WARN ? "warning" : "info";
}

// ---------------------------------------------------------------- rules
Rule *RuleSet::upsert(const Rule &r) {
  if (r.id != 0) {
    if (Rule *ex = find(r.id)) {
      *ex = r;
      return ex;
    }
  }
  if (count >= MAX_RULES) return nullptr;
  Rule nr = r;
  if (nr.id == 0) {
    uint8_t id = 1;
    while (find(id)) id++;
    nr.id = id;
  }
  rules[count++] = nr;
  return &rules[count - 1];
}

bool RuleSet::remove(uint8_t id) {
  for (int i = 0; i < count; i++) {
    if (rules[i].id == id) {
      for (int j = i; j < count - 1; j++) rules[j] = rules[j + 1];
      count--;
      return true;
    }
  }
  return false;
}

static void addRule(RuleSet &rs, const char *name, Metric m, Cmp c, float thr, float hyst,
                    uint16_t delay, Severity sev, Action act = ACT_NONE, bool enabled = true) {
  Rule r;
  strncpy(r.name, name, sizeof(r.name) - 1);
  r.metric = m;
  r.cmp = c;
  r.threshold = thr;
  r.hysteresis = hyst;
  r.delaySec = delay;
  r.sev = sev;
  r.action = act;
  r.enabled = enabled;
  rs.upsert(r);
}

// Sensible starting points for a 12 V lithium caravan setup. Every one of
// these can be edited, disabled or deleted from the touchscreen or web UI.
void loadDefaultRules(RuleSet &rs) {
  rs.count = 0;
  addRule(rs, "Battery low", M_BAT_V, CMP_LT, 12.4f, 0.3f, 60, SEV_WARN);
  addRule(rs, "Battery critical", M_BAT_V, CMP_LT, 11.8f, 0.6f, 30, SEV_ALARM, ACT_INVERTER_OFF);
  addRule(rs, "Battery over-voltage", M_BAT_V, CMP_GT, 14.8f, 0.3f, 10, SEV_ALARM);
  addRule(rs, "Charge below 30%", M_BAT_SOC, CMP_LT, 30, 5, 60, SEV_WARN);
  addRule(rs, "Charge below 15%", M_BAT_SOC, CMP_LT, 15, 5, 60, SEV_ALARM, ACT_AIRCON_OFF);
  addRule(rs, "High load", M_LOAD_A, CMP_GT, 180, 20, 15, SEV_WARN);
  addRule(rs, "Fridge warm", M_TEMP_FRIDGE, CMP_GT, 6, 1, 1200, SEV_ALARM);
  addRule(rs, "Fridge freezing", M_TEMP_FRIDGE, CMP_LT, -1, 1, 1800, SEV_WARN, ACT_NONE, false);
  addRule(rs, "Inside hot", M_TEMP_IN, CMP_GT, 35, 2, 300, SEV_WARN);
  addRule(rs, "Inside cold", M_TEMP_IN, CMP_LT, 5, 1, 300, SEV_WARN);
  addRule(rs, "Tank 1 low", M_TANK1, CMP_LT, 15, 5, 30, SEV_INFO);
  addRule(rs, "Tank 2 low", M_TANK2, CMP_LT, 15, 5, 30, SEV_INFO);
  addRule(rs, "Grey tank full", M_TANK3, CMP_GT, 85, 5, 30, SEV_WARN);
  addRule(rs, "Strong wind forecast", M_WX_GUST, CMP_GT, 50, 10, 0, SEV_WARN);  // bring the awning in
}

// ---------------------------------------------------------------- JSON helpers
// Round floats so the JSON stays short (e.g. 13.27 not 13.270000457763672).
static float r1(float v) { return std::round(v * 10.0f) / 10.0f; }
static float r2(float v) { return std::round(v * 100.0f) / 100.0f; }

static void copyStr(char *dst, size_t n, const char *src) {
  if (!src) src = "";
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

void stateToJson(const State &s, JsonObject o) {
  o["t"] = "state";
  o["ts"] = s.ts;
  o["tz"] = s.tzOff;
  o["seq"] = s.seq;

  JsonObject b = o["bat"].to<JsonObject>();
  b["on"] = s.bat.online;
  b["src"] = s.bat.source;
  b["v"] = r2(s.bat.v);
  b["a"] = r1(s.bat.a);
  b["soc"] = r1(s.bat.soc);
  b["ttg"] = s.bat.ttgMin;
  b["sa"] = r1(s.bat.solarA);
  b["ca"] = r1(s.bat.chargerA);
  b["chg"] = r1(s.bat.chargeA);
  b["load"] = r1(s.bat.loadA);

  JsonArray sol = o["sol"].to<JsonArray>();
  for (auto &x : s.solar) {
    JsonObject j = sol.add<JsonObject>();
    j["on"] = x.online;
    j["pv"] = r1(x.pvV);
    j["w"] = r1(x.pvW);
    j["bv"] = r2(x.batV);
    j["ba"] = r1(x.batA);
    j["cs"] = x.cs;
    j["err"] = x.err;
    j["yt"] = r2(x.yieldTodayKwh);
    j["mx"] = x.maxWToday;
    j["ytot"] = r1(x.yieldTotalKwh);
  }

  JsonArray tmp = o["tmp"].to<JsonArray>();
  for (auto &x : s.temp) {
    JsonObject j = tmp.add<JsonObject>();
    j["on"] = x.online;
    j["c"] = r1(x.c);
    j["lo"] = r1(x.minC);
    j["hi"] = r1(x.maxC);
    if (x.batt >= 0) j["b"] = x.batt;
  }

  JsonArray tk = o["tank"].to<JsonArray>();
  for (auto &x : s.tank) {
    JsonObject j = tk.add<JsonObject>();
    j["on"] = x.online;
    j["p"] = r1(x.pct);
  }

  JsonObject gas = o["gas"].to<JsonObject>();
  gas["on"] = s.gas.online;
  gas["p"] = r1(s.gas.pct);

  JsonArray pm = o["pump"].to<JsonArray>();
  for (auto &x : s.pump) {
    JsonObject j = pm.add<JsonObject>();
    j["on"] = x.on;
    j["sn"] = x.sensed;
    j["lk"] = x.lockout;
    j["rt"] = x.runSec;
  }

  JsonObject inv = o["inv"].to<JsonObject>();
  inv["on"] = s.inv.on;
  inv["ac"] = s.inv.acPresent;

  JsonObject ac = o["ac"].to<JsonObject>();
  ac["on"] = s.ac.on;
  ac["pre"] = s.ac.preset;

  levelToJson(s.lvl, o["lvl"].to<JsonObject>());
  weatherToJson(s.wx, o["wx"].to<JsonObject>());

  JsonArray al = o["alerts"].to<JsonArray>();
  for (int i = 0; i < s.alertCount && i < MAX_ACTIVE_ALERTS; i++) {
    const ActiveAlert &x = s.alerts[i];
    JsonObject j = al.add<JsonObject>();
    j["id"] = x.ruleId;
    j["sev"] = x.sev;
    j["ack"] = x.acked;
    j["since"] = x.since;
    j["val"] = r2(x.value);
    j["msg"] = x.msg;
  }

  JsonObject net = o["net"].to<JsonObject>();
  net["wifi"] = s.wifi;
  net["cloud"] = s.cloud;
  net["ip"] = s.ip;
}

void stateFromJson(State &s, JsonObjectConst o) {
  s.ts = o["ts"] | 0u;
  s.tzOff = o["tz"] | 0;
  s.seq = o["seq"] | 0u;

  JsonObjectConst b = o["bat"];
  s.bat.online = b["on"] | false;
  s.bat.source = b["src"] | 0;
  s.bat.v = b["v"] | 0.0f;
  s.bat.a = b["a"] | 0.0f;
  s.bat.soc = b["soc"] | 0.0f;
  s.bat.ttgMin = b["ttg"] | -1;
  s.bat.solarA = b["sa"] | 0.0f;
  s.bat.chargerA = b["ca"] | 0.0f;
  s.bat.chargeA = b["chg"] | 0.0f;
  s.bat.loadA = b["load"] | 0.0f;

  JsonArrayConst sol = o["sol"];
  for (int i = 0; i < NUM_SOLAR; i++) {
    JsonObjectConst j = sol[i];
    Solar &x = s.solar[i];
    x.online = j["on"] | false;
    x.pvV = j["pv"] | 0.0f;
    x.pvW = j["w"] | 0.0f;
    x.batV = j["bv"] | 0.0f;
    x.batA = j["ba"] | 0.0f;
    x.cs = j["cs"] | 0;
    x.err = j["err"] | 0;
    x.yieldTodayKwh = j["yt"] | 0.0f;
    x.maxWToday = j["mx"] | 0;
    x.yieldTotalKwh = j["ytot"] | 0.0f;
  }

  JsonArrayConst tmp = o["tmp"];
  for (int i = 0; i < NUM_TEMPS; i++) {
    JsonObjectConst j = tmp[i];
    s.temp[i].online = j["on"] | false;
    s.temp[i].c = j["c"] | 0.0f;
    s.temp[i].minC = j["lo"] | 0.0f;
    s.temp[i].maxC = j["hi"] | 0.0f;
    s.temp[i].batt = j["b"] | -1;
  }

  JsonArrayConst tk = o["tank"];
  for (int i = 0; i < NUM_TANKS; i++) {
    s.tank[i].online = tk[i]["on"] | false;
    s.tank[i].pct = tk[i]["p"] | 0.0f;
  }

  s.gas.online = o["gas"]["on"] | false;
  s.gas.pct = o["gas"]["p"] | 0.0f;

  JsonArrayConst pm = o["pump"];
  for (int i = 0; i < NUM_PUMPS; i++) {
    JsonObjectConst j = pm[i];
    s.pump[i].on = j["on"] | false;
    s.pump[i].sensed = j["sn"] | false;
    s.pump[i].lockout = j["lk"] | false;
    s.pump[i].runSec = j["rt"] | 0u;
  }

  s.inv.on = o["inv"]["on"] | false;
  s.inv.acPresent = o["inv"]["ac"] | false;
  s.ac.on = o["ac"]["on"] | false;
  s.ac.preset = o["ac"]["pre"] | -1;

  if (o["lvl"].is<JsonObjectConst>()) levelFromJson(s.lvl, o["lvl"]);
  if (o["wx"].is<JsonObjectConst>()) weatherFromJson(s.wx, o["wx"]);

  JsonArrayConst al = o["alerts"];
  s.alertCount = 0;
  for (JsonObjectConst j : al) {
    if (s.alertCount >= MAX_ACTIVE_ALERTS) break;
    ActiveAlert &x = s.alerts[s.alertCount++];
    x.ruleId = j["id"] | 0;
    x.sev = j["sev"] | 0;
    x.acked = j["ack"] | false;
    x.since = j["since"] | 0u;
    x.value = j["val"] | 0.0f;
    copyStr(x.msg, sizeof(x.msg), j["msg"] | "");
  }

  s.wifi = o["net"]["wifi"] | false;
  s.cloud = o["net"]["cloud"] | false;
  copyStr(s.ip, sizeof(s.ip), o["net"]["ip"] | "");
}

void ruleToJson(const Rule &r, JsonObject o) {
  o["id"] = r.id;
  o["en"] = r.enabled;
  o["name"] = r.name;
  o["m"] = metricInfo(r.metric).key;
  o["cmp"] = r.cmp == CMP_GT ? ">" : "<";
  o["thr"] = r2(r.threshold);
  o["hys"] = r2(r.hysteresis);
  o["dly"] = r.delaySec;
  o["sev"] = r.sev;
  o["ntf"] = r.notify;
  o["act"] = actionKey(r.action);
}

bool ruleFromJson(Rule &r, JsonObjectConst o) {
  Metric m = metricFromKey(o["m"] | "");
  if (m == M_COUNT) return false;
  r.id = o["id"] | 0;
  r.enabled = o["en"] | true;
  copyStr(r.name, sizeof(r.name), o["name"] | metricInfo(m).label);
  r.metric = m;
  const char *cmp = o["cmp"] | "<";
  r.cmp = (cmp[0] == '>') ? CMP_GT : CMP_LT;
  r.threshold = o["thr"] | 0.0f;
  r.hysteresis = o["hys"] | 0.0f;
  if (r.hysteresis < 0) r.hysteresis = 0;
  r.delaySec = o["dly"] | 0;
  uint8_t sev = o["sev"] | (uint8_t)SEV_WARN;
  r.sev = (Severity)(sev > (uint8_t)SEV_ALARM ? (uint8_t)SEV_ALARM : sev);
  r.notify = o["ntf"] | true;
  r.action = actionFromKey(o["act"] | "none");
  return true;
}

void rulesToJson(const RuleSet &rs, JsonArray a) {
  for (int i = 0; i < rs.count; i++) ruleToJson(rs.rules[i], a.add<JsonObject>());
}

void rulesFromJson(RuleSet &rs, JsonArrayConst a) {
  rs.count = 0;
  for (JsonObjectConst o : a) {
    Rule r;
    if (ruleFromJson(r, o)) rs.upsert(r);
  }
}

// Also used on its own as the fast 5 Hz {"t":"lvl"} frame on the RS485 link.
void levelToJson(const Level &l, JsonObject o) {
  o["on"] = l.online;
  o["z"] = l.zeroed;
  o["f"] = l.fwdLearned;
  o["p"] = r2(l.pitch);
  o["r"] = r2(l.roll);
  o["tr"] = l.trackMm;
  o["jk"] = l.jockeyMm;
}

void levelFromJson(Level &l, JsonObjectConst o) {
  l.online = o["on"] | false;
  l.zeroed = o["z"] | false;
  l.fwdLearned = o["f"] | false;
  l.pitch = o["p"] | 0.0f;
  l.roll = o["r"] | 0.0f;
  l.trackMm = o["tr"] | l.trackMm;
  l.jockeyMm = o["jk"] | l.jockeyMm;
}

void metaToJson(const Meta &m, JsonObject o) {
  o["t"] = "meta";
  JsonArray a;
  a = o["sol"].to<JsonArray>();
  for (auto &n : m.solarName) a.add(n);
  a = o["tmp"].to<JsonArray>();
  for (auto &n : m.tempName) a.add(n);
  a = o["tank"].to<JsonArray>();
  for (auto &n : m.tankName) a.add(n);
  a = o["pump"].to<JsonArray>();
  for (auto &n : m.pumpName) a.add(n);
  a = o["acp"].to<JsonArray>();
  for (int i = 0; i < m.acPresetCount; i++) a.add(m.acPresetName[i]);
  o["cap"] = m.batCapacityAh;
  o["rc"] = m.remoteControl;
  o["tcal"] = m.tankCal;
  o["ls"] = m.locSrc;
  o["ln"] = m.locName;
  o["fw"] = m.fw;
}

void metaFromJson(Meta &m, JsonObjectConst o) {
  for (int i = 0; i < NUM_SOLAR; i++)
    if (o["sol"][i].is<const char *>()) copyStr(m.solarName[i], 16, o["sol"][i]);
  for (int i = 0; i < NUM_TEMPS; i++)
    if (o["tmp"][i].is<const char *>()) copyStr(m.tempName[i], 16, o["tmp"][i]);
  for (int i = 0; i < NUM_TANKS; i++)
    if (o["tank"][i].is<const char *>()) copyStr(m.tankName[i], 16, o["tank"][i]);
  for (int i = 0; i < NUM_PUMPS; i++)
    if (o["pump"][i].is<const char *>()) copyStr(m.pumpName[i], 16, o["pump"][i]);
  if (o["acp"].is<JsonArrayConst>()) {
    m.acPresetCount = 0;
    for (JsonVariantConst v : o["acp"].as<JsonArrayConst>()) {
      if (m.acPresetCount >= MAX_AC_PRESETS) break;
      copyStr(m.acPresetName[m.acPresetCount++], 16, v | "");
    }
  }
  m.batCapacityAh = o["cap"] | m.batCapacityAh;
  m.remoteControl = o["rc"] | m.remoteControl;
  m.tankCal = o["tcal"] | m.tankCal;
  m.locSrc = o["ls"] | m.locSrc;
  if (o["ln"].is<const char *>()) copyStr(m.locName, sizeof(m.locName), o["ln"]);
  copyStr(m.fw, sizeof(m.fw), o["fw"] | m.fw);
}

}  // namespace cv
