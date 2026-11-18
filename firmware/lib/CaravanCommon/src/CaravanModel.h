// Shared data model for the Caravan Power system.
//
// This header is used by BOTH firmware images (hub + display) and by the host
// unit tests, so it must not depend on Arduino headers. The JSON wire format
// produced here is the same one used on the RS485 display link, the local
// WebSocket and the cloud MQTT topics, so every client speaks one language.
#pragma once

#include <cstdint>
#include <cstring>
#include <ArduinoJson.h>

#include "TheftGuard.h"
#include "Weather.h"

namespace cv {

constexpr int NUM_SOLAR = 2;
constexpr int NUM_TEMPS = 3;
constexpr int NUM_TANKS = 3;
constexpr int NUM_PUMPS = 2;
constexpr int MAX_RULES = 24;
constexpr int MAX_ACTIVE_ALERTS = 12;
constexpr int MAX_AC_PRESETS = 8;

enum TempId : uint8_t { TEMP_INSIDE = 0, TEMP_OUTSIDE = 1, TEMP_FRIDGE = 2 };

// ---------------------------------------------------------------- live state
struct Solar {
  bool online = false;
  float pvV = 0, pvW = 0;     // panel side
  float batV = 0, batA = 0;   // battery side (charge current)
  uint8_t cs = 0;             // VE.Direct charge state (CS)
  uint16_t err = 0;           // VE.Direct error code (ERR)
  float yieldTodayKwh = 0;    // H20
  uint16_t maxWToday = 0;     // H21
  float yieldTotalKwh = 0;    // H19
};

enum BatterySource : uint8_t { BAT_SRC_NONE = 0, BAT_SRC_SHUNT = 1, BAT_SRC_BMPRO = 2 };
// Battery bank size (set on the phone). Default: 3 x 135 Ah lithium.
constexpr float BAT_CAP_DEFAULT_AH = 405;
constexpr float BAT_CAP_MIN_AH = 20;
constexpr float BAT_CAP_MAX_AH = 2000;

struct Battery {
  bool online = false;
  uint8_t source = BAT_SRC_NONE;
  float v = 0;
  float a = 0;        // net battery current, + charging / - discharging
  float soc = 0;      // 0..100 %
  int32_t ttgMin = -1;  // time-to-go (discharging) or time-to-full (charging), -1 unknown
  float solarA = 0;   // sum of all solar controllers
  float chargerA = 0; // BMPRO charger output (its solar input + DC-DC from the vehicle)
  float acA = 0;      // mains chargers, inferred by elimination (see AcCharge.h)
  bool acOn = false;  // mains bulk charging detected (debounced); Home shows a tile
  float chargeA = 0;  // total charging sources
  float loadA = 0;    // measured, or derived: chargeA - a
};

struct Temp {
  bool online = false;
  float c = 0, minC = 0, maxC = 0;  // min/max since midnight
  int8_t batt = -1;                  // Bluetooth sensor battery %, -1 = n/a
};

struct Tank {
  bool online = false;
  float pct = 0;
};

// LPG bottle level from a BMPRO SmartSense gas sensor (ultrasonic, under the
// bottle), relayed by the OdysseyLink. Decoding is still a stub on the hub.
struct Gas {
  bool online = false;
  float pct = 0;
};

struct Pump {
  bool on = false;       // commanded / believed state
  bool sensed = false;   // voltage seen on pump output (if sense input fitted)
  bool lockout = false;  // dry-run / max-runtime protection tripped
  uint32_t runSec = 0;   // current run duration
};

struct Inverter {
  bool on = false;
  bool acPresent = false;
};

struct Aircon {
  bool on = false;
  int8_t preset = -1;   // -1 = off / unknown
};

// Diesel heater (Power Science 2 kW in Beau's van). The hub talks to it over
// Bluetooth (the AirHeaterBLE app's protocol, see AirHeater.h) or, as a
// fallback, sends 433 MHz remote codes (no status comes back that way).
enum HeaterLink : uint8_t { HTR_LINK_NONE = 0, HTR_LINK_BLE = 1, HTR_LINK_RF = 2 };
enum HeaterPhase : uint8_t { HTR_OFF = 0, HTR_STARTING = 1, HTR_RUNNING = 2, HTR_COOLING = 3 };
enum HeaterMode : uint8_t { HTR_MODE_TEMP = 0, HTR_MODE_LEVEL = 1 };  // thermostat / fixed power level
constexpr int HEATER_MIN_C = 8, HEATER_MAX_C = 36, HEATER_LEVELS = 10;

struct Heater {
  uint8_t link = HTR_LINK_NONE;  // NONE = no heater set up: the screens hide the heater card
  bool online = false;    // status heard recently (Bluetooth). RF never is.
  uint8_t phase = HTR_OFF;
  uint8_t mode = HTR_MODE_TEMP;
  int8_t setC = 20;       // thermostat set point, HEATER_MIN_C..HEATER_MAX_C
  uint8_t level = 5;      // power level 1..HEATER_LEVELS
  float cabinC = 0;       // the heater's own air-intake / remote sensor
  float bodyC = 0;        // heat exchanger (case) temperature
  float supplyV = 0;
  uint8_t err = 0;        // heater error code, 0 = none (see heaterErrorText)
  bool running() const { return phase == HTR_STARTING || phase == HTR_RUNNING; }
  // Readings (cabin, body, volts, error) only mean something with Bluetooth status.
  bool hasStatus() const { return link == HTR_LINK_BLE && online; }
};
const char *heaterPhaseText(uint8_t phase);
const char *heaterErrorText(uint8_t err);

// A heater command from any client: {"c":"heater","on":true} / {"mode":"temp"|"level"}
// / {"temp":22} / {"level":6}. Fields may be combined; -1 = not given.
struct HeaterCmd {
  int8_t on = -1, mode = -1, temp = -1, level = -1;
  bool any() const { return on >= 0 || mode >= 0 || temp >= 0 || level >= 0; }
};
// Parses and range-checks; on failure *why says what's wrong.
bool heaterCmdFromJson(JsonObjectConst o, HeaterCmd &c, const char **why);

// Caravan level (MPU-6050 on the hub). pitch + = front high, roll + = left high.
struct Level {
  bool online = false;
  bool zeroed = false;      // "Set level" done
  bool fwdLearned = false;  // "Learn front" done
  float pitch = 0, roll = 0;
  uint16_t trackMm = 1900, jockeyMm = 4500;
};

// SEV_CRIT is the top step of a stepped rule (e.g. battery at 5 %): shown and
// pushed like an alarm but louder/redder. Anything >= SEV_ALARM sounds the buzzer.
enum Severity : uint8_t { SEV_INFO = 0, SEV_WARN = 1, SEV_ALARM = 2, SEV_CRIT = 3 };

struct ActiveAlert {
  uint8_t ruleId = 0;
  uint8_t sev = SEV_INFO;
  bool acked = false;
  uint32_t since = 0;   // unix seconds (0 if clock not yet set)
  float value = 0;
  char msg[64] = {0};
};

struct State {
  uint32_t ts = 0;      // unix time from hub (NTP)
  int32_t tzOff = 0;    // hub's local time offset from UTC in seconds (DST aware)
  uint32_t seq = 0;
  Solar solar[NUM_SOLAR];
  Battery bat;
  Temp temp[NUM_TEMPS];
  Tank tank[NUM_TANKS];
  Gas gas;
  Pump pump[NUM_PUMPS];
  Inverter inv;
  Aircon ac;
  Heater htr;
  Level lvl;
  Secure sec;  // secure mode (anti-theft), see TheftGuard.h
  Weather wx;
  uint8_t alertCount = 0;
  ActiveAlert alerts[MAX_ACTIVE_ALERTS];
  bool wifi = false, cloud = false;
  char ip[16] = {0};

  float solarW() const {
    float w = 0;
    for (auto &s : solar) if (s.online) w += s.pvW;
    return w;
  }
};

// ---------------------------------------------------------------- alert rules
enum Metric : uint8_t {
  M_BAT_V = 0, M_BAT_SOC, M_BAT_A, M_LOAD_A, M_CHARGE_A,
  M_SOLAR_W, M_SOLAR1_W, M_SOLAR2_W,
  M_TEMP_IN, M_TEMP_OUT, M_TEMP_FRIDGE,
  M_TANK1, M_TANK2, M_TANK3,
  M_WX_GUST, M_WX_RAIN,   // today's forecast max wind gust (km/h), rain chance (%)
  M_GAS,  // appended: rules refer to metrics by key, but keep the order stable
  M_THEFT,  // secure mode: 0 quiet, else the SecureCause that set the alarm off
  M_COUNT
};

struct MetricInfo {
  const char *key;    // stable id used in JSON
  const char *label;  // human label
  const char *unit;
  float step;         // UI +/- step
};
const MetricInfo &metricInfo(Metric m);
Metric metricFromKey(const char *key);
// Returns false if the metric's source is offline (rule is then not evaluated).
bool metricValue(const State &s, Metric m, float &out);

enum Cmp : uint8_t { CMP_LT = 0, CMP_GT = 1 };

// Optional automatic action when an alert fires (load shedding / protection).
enum Action : uint8_t { ACT_NONE = 0, ACT_AIRCON_OFF, ACT_INVERTER_OFF, ACT_PUMPS_OFF, ACT_COUNT };
const char *actionKey(Action a);
Action actionFromKey(const char *k);

// A rule can step up through extra levels after its first threshold, e.g.
// charge < 30 % warning, then < 15 % alarm, then < 5 % critical. Each level is
// strictly deeper than the one before (lower for "<", higher for ">") and
// fires once; it re-arms only after the value recovers past it + hysteresis.
constexpr int MAX_LEVELS = 2;  // extra levels on top of threshold/sev
struct RuleLevel {
  float threshold = 0;
  Severity sev = SEV_ALARM;
};

struct Rule {
  uint8_t id = 0;
  bool enabled = true;
  char name[28] = {0};
  Metric metric = M_BAT_V;
  Cmp cmp = CMP_LT;
  float threshold = 0;
  float hysteresis = 0;   // must recover past threshold +/- hysteresis to clear
  uint16_t delaySec = 0;  // condition must hold this long before firing
  Severity sev = SEV_WARN;
  bool notify = true;     // push to phone
  Action action = ACT_NONE;  // runs when the rule reaches its deepest level
  uint8_t levelCount = 0;
  RuleLevel levels[MAX_LEVELS];
  // Level 0 is threshold/sev; 1..levelCount are levels[].
  float levelThreshold(int k) const { return k <= 0 ? threshold : levels[k - 1].threshold; }
  Severity levelSev(int k) const { return k <= 0 ? sev : levels[k - 1].sev; }
};

struct RuleSet {
  uint8_t count = 0;
  Rule rules[MAX_RULES];
  Rule *find(uint8_t id) {
    for (int i = 0; i < count; i++) if (rules[i].id == id) return &rules[i];
    return nullptr;
  }
  // Insert or replace by id. id==0 means "new": a free id is allocated.
  Rule *upsert(const Rule &r);
  bool remove(uint8_t id);
};

void loadDefaultRules(RuleSet &rs);
// Secure mode raises its alarm through a rule; puts the default one back if
// it's missing (older saved rule lists, or deleted). Returns true if added.
bool ensureTheftRule(RuleSet &rs);

// ---------------------------------------------------------------- metadata
// Slow-changing names/labels, sent on connect and whenever they change.
struct Meta {
  char solarName[NUM_SOLAR][16] = {"Roof", "Portable"};
  char tempName[NUM_TEMPS][16] = {"Inside", "Outside", "Fridge"};
  char tankName[NUM_TANKS][16] = {"Tank 1", "Tank 2", "Grey"};
  char pumpName[NUM_PUMPS][16] = {"Internal", "External"};
  uint8_t acPresetCount = 0;
  char acPresetName[MAX_AC_PRESETS][16] = {};
  float batCapacityAh = BAT_CAP_DEFAULT_AH;
  uint8_t locSrc = 0;      // weather location source (see LocationSource)
  char locName[24] = "";   // weather location name
  bool remoteControl = true;
  bool tankCal = false;  // true when tanks are wired taps that need calibrating
  bool secCodeSet = false;  // a disarm code is set for the touchscreen (set from the phone)
  uint16_t screenSleepSec = 300;  // touchscreen dims after this many idle seconds, 0 = never (set from the phone)
  char fw[16] = "1.0.0";
};
void levelToJson(const Level &l, JsonObject o);
void levelFromJson(Level &l, JsonObjectConst o);
void secureToJson(const Secure &x, JsonObject o);
void secureFromJson(Secure &x, JsonObjectConst o);
void metaToJson(const Meta &m, JsonObject o);
void metaFromJson(Meta &m, JsonObjectConst o);

// ---------------------------------------------------------------- JSON
// Compact keys keep a full state frame ~1 KB so it fits comfortably on a
// 115200 baud RS485 link at 1 Hz and in a single MQTT packet.
void stateToJson(const State &s, JsonObject o);
void stateFromJson(State &s, JsonObjectConst o);
void ruleToJson(const Rule &r, JsonObject o);
bool ruleFromJson(Rule &r, JsonObjectConst o);
void rulesToJson(const RuleSet &rs, JsonArray a);
void rulesFromJson(RuleSet &rs, JsonArrayConst a);

const char *chargeStateName(uint8_t cs);
const char *severityName(uint8_t sev);

}  // namespace cv
