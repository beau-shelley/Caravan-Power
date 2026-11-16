// Caravan Power - HUB firmware
// Reads every system in the van, runs the alert rules, drives the pumps /
// inverter / aircon, and serves the data to the 7" display (RS485), the local
// web dashboard (Wi-Fi) and the cloud (MQTT over TLS) with phone push alerts.
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <cctype>
#include <LittleFS.h>
#include <Wire.h>

#include "AcCharge.h"
#include "AlertEngine.h"
#include "Bmpro.h"
#include "CaravanModel.h"
#include "Comms.h"
#include "Demo.h"
#include "Heater.h"
#include "HubConfig.h"
#include "Inputs.h"
#include "Outputs.h"
#include "VictronBle.h"
#include "BleTempRx.h"
#include "BleSurvey.h"
#include "LevelSensor.h"
#include "Secure.h"
#include "WeatherClient.h"
#include "VictronBleRx.h"
#include "pins.h"

using namespace hub;

static cv::State gState;
static cv::AlertEngine gAlerts;
static cv::AcChargeDetector gAcCharge;  // mains chargers, inferred by elimination
static VictronPort gVictron[cv::NUM_SOLAR];
static TempSensors gTemps;
static TankInputs gTanks;
static BatteryShunt gShunt;
static SwitchedOutput gPump[cv::NUM_PUMPS];
static SwitchedOutput gInverter;
static AirconIr gAircon;
static bool gBmproOn = false;
static bool gVictronBleOn = false;
static cv::NmeaParser gGps;
static bool gGpsOn = false;
static uint32_t gGpsFixMs = 0;
static bool gBleTempsOn = false;

static void configChanged();

// Sets the weather location (phone, town search or GPS) and refreshes the forecast.
static bool applyLocation(float lat, float lon, const char *name, uint8_t src, const char **why) {
  if (lat < -90 || lat > 90 || lon < -180 || lon > 180 || (lat == 0 && lon == 0)) {
    if (why) *why = "bad coordinates";
    return false;
  }
  if (src != cv::LOC_GPS && gGpsOn && gGpsFixMs && millis() - gGpsFixMs < 600000) {
    if (why) *why = "The GPS has a fix - it sets the location automatically";
    return false;
  }
  LocationCfg &loc = gConfig.location;
  loc.src = src;
  loc.lat = lat;
  loc.lon = lon;
  snprintf(loc.name, sizeof(loc.name), "%s", name ? name : "");
  gConfig.meta.locSrc = src;
  memcpy(gConfig.meta.locName, loc.name, sizeof(loc.name));
  gWeather.refreshSoon();
  return true;
}

// GPS fixes update the weather location (saved only after moving > 5 km, so
// the last known spot survives a reboot without wearing out the flash).
static void gpsLoop(uint32_t now) {
  if (!gGpsOn) return;
  while (GPS_SERIAL.available()) {
    if (!gGps.feed((uint8_t)GPS_SERIAL.read())) continue;
    gGpsFixMs = now;
    LocationCfg &loc = gConfig.location;
    float lat = (float)gGps.lat(), lon = (float)gGps.lon();
    if (loc.src != cv::LOC_GPS || cv::distanceKm(lat, lon, loc.lat, loc.lon) > 5) {
      applyLocation(lat, lon, "", cv::LOC_GPS, nullptr);
      configChanged();
      Serial.printf("[gps] location %.4f, %.4f\n", lat, lon);
    }
  }
}

static void startBleTemps() {
  if (gBleTempsOn) return;
  gBleTemps.begin();
  gBleTempsOn = true;
}

static bool gVeDirectOn[cv::NUM_SOLAR] = {};

static void startSolar(int i) {
  const VictronCfg &v = gConfig.victron[i];
  if (v.src == SOLAR_VEDIRECT && !gVeDirectOn[i]) {
    if (i == GPS_SHARES_SOLAR) gGpsOn = false;  // this UART is shared with the optional GPS
    if (i == 0) {
      gVictron[0].begin(Serial1, PIN_VE1_RX, PIN_VE1_TX);
      gVeDirectOn[i] = true;
    } else {
#if HUB_HAS_VE2
      gVictron[1].begin(Serial2, PIN_VE2_RX, PIN_VE2_TX);
      gVeDirectOn[i] = true;
#else
      Serial.println("[victron] solar 2: no spare UART for a VE.Direct cable on this board - use Bluetooth");
#endif
    }
  }
  if (v.src == SOLAR_BLE) {
    if (!gVictronBleOn) {
      gVictronBle.begin();
      gVictronBleOn = true;
    }
    if (!gVictronBle.setDevice(i, v.addr, v.key))
      Serial.printf("[victron] solar %d: no valid key yet - add it in the dashboard Setup tab\n", i + 1);
  }
}
static uint32_t gBmproPumpSince[cv::NUM_PUMPS] = {};
static bool gBmproPumpTripped[cv::NUM_PUMPS] = {};

// Switch a pump through whichever path it's configured for.
static bool switchPump(int i, bool on, const char **why) {
  if (gConfig.pumpViaBmpro(i)) {
    if (!gBmproOn) {
      if (why) *why = "BMPRO link not running";
      return false;
    }
    return gBmpro.setPump(i, on, why);
  }
  return gPump[i].set(on, why);
}

// Diesel heater: whichever driver the config picks (the simulated one in the
// fake-data build). nullptr = none set up, or Bluetooth in the listen-only
// survey build (which must never connect to anything).
static HeaterDriver *heaterDriver() {
  if (demo::kOn) return &demo::heater();
  switch (gConfig.heater.link) {
    case cv::HTR_LINK_BLE: return survey::kOn ? nullptr : &gHeaterBle;
    case cv::HTR_LINK_RF: return &gHeater433;
    default: return nullptr;
  }
}

static void startHeater() {
  if (demo::kOn || survey::kOn) return;
  if (gConfig.heater.link == cv::HTR_LINK_BLE) gHeaterBle.setTarget(gConfig.heater.addr, gConfig.heater.passkey);
  else gHeaterBle.release();  // switched away from Bluetooth: let go of the heater
}

static void heaterLoop() {
  static uint8_t lastErr = 0;
  HeaterDriver *d = heaterDriver();
  if (!d) {
    gState.htr = cv::Heater();
    gState.htr.link = demo::kOn ? cv::HTR_LINK_BLE : gConfig.heater.link;
    return;
  }
  gState.htr.link = demo::kOn ? cv::HTR_LINK_BLE : gConfig.heater.link;
  d->loop(gState);
  // A heater fault (out of fuel, didn't light...) goes to the phone once.
  uint8_t err = gState.htr.hasStatus() ? gState.htr.err : 0;
  if (err && err != lastErr) gNotify.push("Diesel heater", cv::heaterErrorText(err), cv::SEV_ALARM);
  lastErr = err;
}

// ---------------------------------------------------------------- helpers
static void applyOutputConfig() {
  for (int i = 0; i < cv::NUM_PUMPS; i++)
    gPump[i].configure(gConfig.pumps[i].mode, gConfig.pumps[i].pulseMs, gConfig.pumps[i].useSense);
  gInverter.configure(gConfig.inverter.mode, gConfig.inverter.pulseMs, gConfig.inverter.useSense);
}

static void stateChanged() {
  gLink.requestStateSoon();
  gWeb.broadcastStateSoon();
  gCloud.publishStateSoon();
}

static void configChanged() {
  gConfig.save();
  gCloud.publishMetaSoon();
  gLink.pushConfigSoon();
}

static void replyRules(JsonDocument &reply) {
  JsonObject o = reply.to<JsonObject>();
  o["t"] = "rules";
  cv::rulesToJson(gConfig.rules, o["rules"].to<JsonArray>());
}

static void replyErr(JsonDocument &reply, const char *msg) {
  reply.clear();
  reply["t"] = "err";
  reply["msg"] = msg;
}

static void setAircon(int preset) {
  if (gAircon.sendPreset(preset)) {
    gState.ac.on = preset >= 0;
    gState.ac.preset = preset;
  }
}

static void runAction(cv::Action a) {
  switch (a) {
    case cv::ACT_AIRCON_OFF: setAircon(-1); break;
    case cv::ACT_INVERTER_OFF: gInverter.set(false); break;
    case cv::ACT_PUMPS_OFF:
      for (int i = 0; i < cv::NUM_PUMPS; i++) switchPump(i, false, nullptr);
      break;
    default: break;
  }
}

// Alert transitions -> phone push, cloud event, automatic actions.
static void onAlert(const cv::AlertEngine::Event &ev, void *) {
  bool raised = ev.type == cv::AlertEngine::RAISED;
  Serial.printf("[alert] %s %s\n", raised ? "RAISED" : "cleared", ev.msg);
  if (ev.rule.notify && ev.rule.name[0]) gNotify.push(ev.rule.name, ev.msg, ev.rule.sev, !raised);
  if (raised && ev.rule.action != cv::ACT_NONE) runAction(ev.rule.action);

  JsonDocument d;
  d["t"] = "event";
  d["type"] = raised ? "raised" : "cleared";
  d["id"] = ev.rule.id;
  d["sev"] = ev.rule.sev;
  d["msg"] = ev.msg;
  d["act"] = cv::actionKey(ev.rule.action);
  d["ts"] = gState.ts;
  gCloud.queueEvent(d);
  stateChanged();
}

// Secure mode needs real movement data: the MPU-6050, or in the fake-data
// build the simulated sensor (used whenever no real one is plugged in).
static bool secureSensorOk() { return demo::kOn || gLevel.online(); }

// "Restart system" from the touchscreen (or phone): reply first, restart 1 s
// later so the reply has time to reach the display over RS485.
static uint32_t gRestartAtMs = 0;

// ---------------------------------------------------------------- commands
// One handler for every client (touchscreen, local web, cloud). Messages look
// like {"c":"pump","i":0,"on":true}. See docs/PROTOCOL.md for the full list.
static bool isMutating(const char *c) {
  static const char *readOnly[] = {"state", "hello", "rules_get", "sensors", "bmpro_log", "victron_seen", "ble_sensors", "level_status",
                                   "bmpro_devices", "ac_learn_status", "ping", "heater_seen", "heater_log"};
  for (auto r : readOnly)
    if (strcmp(c, r) == 0) return false;
  return true;
}

void hub::handleCommand(JsonObjectConst cmd, Source src, JsonDocument &reply) {
  StateLock lock;
  const char *c = cmd["c"] | "";

  // Arming secure mode from away is always allowed (forgot to arm it);
  // disarming from the cloud follows the remote-control setting.
  bool armOnly = !strcmp(c, "secure") && (cmd["arm"] | false) && cmd["sens"].isNull();
  if (src == SRC_CLOUD && isMutating(c) && !gConfig.meta.remoteControl && !armOnly) {
    replyErr(reply, "Remote control is disabled on the caravan display");
    return;
  }

  if (!strcmp(c, "restart")) {
    Serial.println("[sys] restart requested - restarting in 1 s");
    reply["t"] = "restarting";
    gRestartAtMs = millis() | 1;
  } else if (!strcmp(c, "ping")) {
    reply["t"] = "pong";
  } else if (!strcmp(c, "state")) {
    cv::stateToJson(gState, reply.to<JsonObject>());
  } else if (!strcmp(c, "hello")) {
    JsonObject o = reply.to<JsonObject>();
    o["t"] = "hello";
    cv::metaToJson(gConfig.meta, o["meta"].to<JsonObject>());
    cv::rulesToJson(gConfig.rules, o["rules"].to<JsonArray>());
  } else if (!strcmp(c, "rules_get")) {
    replyRules(reply);

  // ---------------- alert rules
  } else if (!strcmp(c, "rule_set")) {
    cv::Rule r;
    if (!cv::ruleFromJson(r, cmd["rule"])) return replyErr(reply, "invalid rule");
    if (!gConfig.rules.upsert(r)) return replyErr(reply, "too many rules");
    configChanged();
    replyRules(reply);
  } else if (!strcmp(c, "rule_del")) {
    gConfig.rules.remove(cmd["id"] | 0);
    configChanged();
    replyRules(reply);
  } else if (!strcmp(c, "rules_reset")) {
    cv::loadDefaultRules(gConfig.rules);
    configChanged();
    replyRules(reply);
  } else if (!strcmp(c, "ack")) {
    gAlerts.ackAll(gState);
    stateChanged();

  // ---------------- loads
  } else if (!strcmp(c, "pump")) {
    int i = cmd["i"] | -1;
    bool on = cmd["on"] | false;
    if (i < 0 || i >= cv::NUM_PUMPS) return replyErr(reply, "bad pump");
    const PumpCfg &pc = gConfig.pumps[i];
    if (on && gState.pump[i].lockout)
      return replyErr(reply, "Pump locked out - tank is empty");
    if (!on && pc.mode == SW_LATCH && !gPump[i].commanded() && gPump[i].sensed())
      return replyErr(reply, "Pump was switched on at the BMPRO - switch it off there");
    const char *why = nullptr;
    if (!switchPump(i, on, &why)) return replyErr(reply, why);
    stateChanged();
  } else if (!strcmp(c, "inverter")) {
    const char *why = nullptr;
    if (!gInverter.set(cmd["on"] | false, &why)) return replyErr(reply, why);
    stateChanged();
  } else if (!strcmp(c, "aircon")) {
    int p = (cmd["off"] | false) ? -1 : (int)(cmd["preset"] | -1);
    if (p >= gConfig.meta.acPresetCount) return replyErr(reply, "no such preset");
    if (!gAircon.hasCode(p)) return replyErr(reply, "IR code not learned yet");
    setAircon(p);
    stateChanged();

  // ---------------- diesel heater
  } else if (!strcmp(c, "heater")) {
    // {"c":"heater","on":true} {"mode":"temp"|"level"} {"temp":22} {"level":6}
    cv::HeaterCmd hc;
    const char *why = nullptr;
    if (!cv::heaterCmdFromJson(cmd, hc, &why)) return replyErr(reply, why);
    HeaterDriver *d = heaterDriver();
    if (!d)
      return replyErr(reply, survey::kOn && gConfig.heater.link == cv::HTR_LINK_BLE
                                 ? "Bluetooth survey build: listen-only, nothing is sent to the heater"
                                 : "No diesel heater set up - add it in Setup on the phone");
    if (!d->send(hc, gState.htr, &why)) return replyErr(reply, why);
    stateChanged();
  } else if (!strcmp(c, "heater_set")) {
    // {"c":"heater_set","link":1,"addr":"aa:bb:..","pass":1234}  link 0 none, 1 Bluetooth, 2 433 MHz
    int link = cmd["link"] | (int)gConfig.heater.link;
    const char *addr = cmd["addr"] | (const char *)gConfig.heater.addr;
    long pass = cmd["pass"] | (long)gConfig.heater.passkey;
    if (link < cv::HTR_LINK_NONE || link > cv::HTR_LINK_RF) return replyErr(reply, "bad heater link");
    if (addr[0] && strlen(addr) != 17) return replyErr(reply, "address must look like aa:bb:cc:dd:ee:ff");
    if (pass < 0 || pass > 9999) return replyErr(reply, "passkey is 4 digits");
    gConfig.heater.link = (uint8_t)link;
    snprintf(gConfig.heater.addr, sizeof(gConfig.heater.addr), "%s", addr);
    gConfig.heater.passkey = (uint16_t)pass;
    configChanged();
    startHeater();
    stateChanged();
    reply["t"] = "ok";
  } else if (!strcmp(c, "heater_seen")) {
    // Heaters in Bluetooth range (starts listening if needed; passive in the survey build).
    if (!survey::kOn) gHeaterBle.listen();
    reply["t"] = "heater_seen";
    reply["list"] = serialized(survey::kOn ? String("[]") : gHeaterBle.seen());
  } else if (!strcmp(c, "heater_log")) {
    reply["t"] = "heater_log";
    reply["log"] = demo::kOn ? String("Fake-data build: the heater is simulated") : gHeaterBle.log();

  } else if (!strcmp(c, "ac_learn")) {
    // slot: -1 = OFF code, 0..n-1 = re-learn existing, n = new preset
    int slot = cmd["slot"] | gConfig.meta.acPresetCount;
    if (slot < -1 || slot > gConfig.meta.acPresetCount || slot >= cv::MAX_AC_PRESETS)
      return replyErr(reply, "bad slot");
    gAircon.startLearn(slot, cmd["name"] | "Preset");
    reply["t"] = "ac_learn";
    reply["state"] = "listening";
  } else if (!strcmp(c, "ac_learn_status")) {
    reply["t"] = "ac_learn";
    reply["state"] = gAircon.learning() ? "listening"
                     : gAircon.learnResult() == 1 ? "ok"
                     : gAircon.learnResult() == -1 ? "timeout" : "idle";
  } else if (!strcmp(c, "ac_del")) {
    gAircon.deletePreset(cmd["slot"] | -2);
    configChanged();

  // ---------------- calibration / setup
  } else if (!strcmp(c, "tank_cal")) {
    int i = cmd["i"] | -1;
    if (i < 0 || i >= cv::NUM_TANKS) return replyErr(reply, "bad tank");
    if (gConfig.tankSource == TANK_BMPRO)
      return replyErr(reply, "Tank levels come from the BMPRO - calibrate them in the Odyssey app");
    const char *pt = cmd["point"] | "";
    float mv = cmd["mv"] | gTanks.rawMv(i);  // explicit value or "use current reading"
    if (!strcmp(pt, "empty")) gConfig.tanks[i].emptyMv = mv;
    else if (!strcmp(pt, "full")) gConfig.tanks[i].fullMv = mv;
    else return replyErr(reply, "point must be empty|full");
    configChanged();
    reply["t"] = "tank_cal";
    reply["i"] = i;
    reply["mv"] = mv;
  } else if (!strcmp(c, "soc_set")) {
    gShunt.setSoc(cmd["soc"] | 100.0f);
    gShunt.persist(true);
  } else if (!strcmp(c, "sensors")) {
    FoundSensor f[8];
    int n = gTemps.found(f, 8);
    JsonObject o = reply.to<JsonObject>();
    o["t"] = "sensors";
    JsonArray a = o["list"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
      JsonObject j = a.add<JsonObject>();
      j["addr"] = f[i].addr;
      j["c"] = f[i].c;
      j["role"] = f[i].role;
    }
  } else if (!strcmp(c, "sensor_assign")) {
    int role = cmd["role"] | -1;
    const char *addr = cmd["addr"] | "";
    if (role < 0 || role >= cv::NUM_TEMPS || strlen(addr) != 16) return replyErr(reply, "bad sensor");
    for (auto &a : gConfig.tempAddr)
      if (!strcmp(a, addr)) a[0] = 0;  // a sensor can only have one role
    strcpy(gConfig.tempAddr[role], addr);
    gConfig.tempSrc[role] = TEMP_WIRED;
    configChanged();
  } else if (!strcmp(c, "remote_control")) {
    if (src == SRC_CLOUD) return replyErr(reply, "can only be changed in the caravan");
    gConfig.meta.remoteControl = cmd["on"] | false;
    configChanged();
    cv::metaToJson(gConfig.meta, reply.to<JsonObject>());
  } else if (!strcmp(c, "display_set")) {
    // Touchscreen preferences live on the hub so they can be set from the phone.
    // sleep: idle seconds before the screen dims right down, 0 = never.
    long sleep = cmd["sleep"] | -1L;
    if (sleep < 0 || sleep > 14400) return replyErr(reply, "sleep must be 0-14400 seconds");
    gConfig.meta.screenSleepSec = (uint16_t)sleep;
    configChanged();
    cv::metaToJson(gConfig.meta, reply.to<JsonObject>());
  } else if (!strcmp(c, "config_set")) {
    bool rc = gConfig.meta.remoteControl;
    gConfig.fromJson(cmd["cfg"]);
    if (src == SRC_CLOUD) gConfig.meta.remoteControl = rc;
    applyOutputConfig();
    startHeater();
    configChanged();
    reply["t"] = "ok";

  // ---------------- Victron Bluetooth
  } else if (!strcmp(c, "victron_set")) {
    // {"c":"victron_set","i":0,"key":"<32 hex>","addr":"aa:bb:..","src":0}
    int i = cmd["i"] | -1;
    if (i < 0 || i >= cv::NUM_SOLAR) return replyErr(reply, "bad controller");
    VictronCfg &v = gConfig.victron[i];
    uint8_t src = cmd["src"] | v.src;
    char key[33], addr[18];
    snprintf(key, sizeof(key), "%s", cmd["key"] | (const char *)v.key);
    snprintf(addr, sizeof(addr), "%s", cmd["addr"] | (const char *)v.addr);
    uint8_t k[16];
    if (src == SOLAR_BLE && !cv::victronKeyFromHex(key, k))
      return replyErr(reply, "Key must be the 32 hex characters from VictronConnect");
    if (addr[0] && strlen(addr) != 17) return replyErr(reply, "address must look like aa:bb:cc:dd:ee:ff");
    v.src = src;
    memcpy(v.key, key, sizeof(v.key));
    memcpy(v.addr, addr, sizeof(v.addr));
    configChanged();
    startSolar(i);
    reply["t"] = "ok";
  // ---------------- weather location
  } else if (!strcmp(c, "location_set")) {
    // {"c":"location_set","lat":-32.6,"lon":149.6,"name":"Mudgee NSW","src":2}
    const char *why = nullptr;
    if (!applyLocation(cmd["lat"] | 999.0f, cmd["lon"] | 999.0f, cmd["name"] | "",
                       cmd["src"] | (uint8_t)cv::LOC_MANUAL, &why))
      return replyErr(reply, why);
    configChanged();
    reply["t"] = "ok";
  } else if (!strcmp(c, "location_search")) {
    // Town search for the touchscreen; results arrive as {"t":"loc_results"}.
    const char *q = cmd["q"] | "";
    if (strlen(q) < 2) return replyErr(reply, "Type at least 2 letters");
    gWeather.search(q);
  } else if (!strcmp(c, "weather_refresh")) {
    gWeather.refreshSoon();

  // ---------------- secure mode (anti-theft)
  } else if (!strcmp(c, "secure")) {
    // {"c":"secure","arm":true|false} and/or {"sens":0|1|2} (low / medium / high)
    if (!cmd["sens"].isNull()) {
      int sens = cmd["sens"] | -1;
      if (sens < cv::SENS_LOW || sens > cv::SENS_HIGH) return replyErr(reply, "sens must be 0, 1 or 2");
      secure::setSensitivity((uint8_t)sens);
    }
    if (!cmd["arm"].isNull()) {
      // Disarming at the touchscreen needs the code (the phone is behind its own lock screen).
      bool arm = cmd["arm"] | false;
      const char *code = gConfig.secure.code;
      if (!arm && src == SRC_DISPLAY && code[0] && strcmp(cmd["code"] | "", code) != 0)
        return replyErr(reply, "Wrong code");
      const char *why = nullptr;
      if (!secure::arm(cmd["arm"] | false, secureSensorOk(), millis(), &why)) return replyErr(reply, why);
      configChanged();  // the theft rule may have been put back
    }
    secure::loop(gState, secureSensorOk(), millis());
    stateChanged();
    reply["t"] = "ok";
  } else if (!strcmp(c, "secure_code")) {
    // {"c":"secure_code","code":"1234"}: the code for disarming at the touchscreen
    // (4-8 digits, "" = none). Set from the phone only, never from the display.
    if (src == SRC_DISPLAY) return replyErr(reply, "Set the code from the phone");
    const char *code = cmd["code"] | "";
    size_t n = strlen(code);
    bool digits = true;
    for (size_t i = 0; i < n; i++) digits &= isdigit((unsigned char)code[i]) != 0;
    if (!digits || (n && (n < 4 || n > 8))) return replyErr(reply, "The code must be 4 to 8 digits");
    snprintf(gConfig.secure.code, sizeof(gConfig.secure.code), "%s", code);
    gConfig.meta.secCodeSet = n > 0;
    configChanged();
    cv::metaToJson(gConfig.meta, reply.to<JsonObject>());

  // ---------------- levelling
  } else if (!strcmp(c, "level_zero")) {
    gLevel.startZero();
    reply["t"] = "level";
    reply["msg"] = "Measuring - keep still for 2 seconds";
  } else if (!strcmp(c, "level_front")) {
    gLevel.startLearnFront();
    reply["t"] = "level";
    reply["msg"] = gConfig.level.zeroed ? "Measuring - keep still for 2 seconds" : "Set level first";
  } else if (!strcmp(c, "level_reset")) {
    gLevel.resetCalibration();
  } else if (!strcmp(c, "level_dims")) {
    gLevel.setDims(cmd["track"] | gConfig.level.trackMm, cmd["jockey"] | gConfig.level.jockeyMm);
  } else if (!strcmp(c, "level_status")) {
    reply["t"] = "level";
    reply["msg"] = gLevel.lastResult();

  } else if (!strcmp(c, "ble_sensors")) {
    // Lists Bluetooth thermometers in range (starts listening if needed).
    startBleTemps();
    reply["t"] = "ble_sensors";
    reply["list"] = serialized(gBleTemps.seen());
  } else if (!strcmp(c, "temp_set")) {
    // {"c":"temp_set","role":2,"src":1,"addr":"aa:bb:.."}  src 0 = wired DS18B20
    int role = cmd["role"] | -1;
    if (role < 0 || role >= cv::NUM_TEMPS) return replyErr(reply, "bad role");
    uint8_t src = cmd["src"] | (uint8_t)TEMP_BLE;
    const char *addr = cmd["addr"] | "";
    if (src == TEMP_BLE && strlen(addr) != 17) return replyErr(reply, "address must look like aa:bb:cc:dd:ee:ff");
    gConfig.tempSrc[role] = src;
    snprintf(gConfig.tempBle[role], sizeof(gConfig.tempBle[role]), "%s", src == TEMP_BLE ? addr : "");
    gState.temp[role] = cv::Temp();  // fresh min/max for the new sensor
    if (src == TEMP_BLE) startBleTemps();
    configChanged();
    reply["t"] = "ok";
  } else if (!strcmp(c, "victron_seen")) {
    reply["t"] = "victron_seen";
    reply["list"] = serialized(gVictronBleOn ? gVictronBle.seen() : String("[]"));

  // ---------------- BMPRO capture
  } else if (!strcmp(c, "bmpro_log")) {
    reply["t"] = "bmpro_log";
    reply["log"] = gBmproOn ? gBmpro.log() : String("BMPRO link not running (nothing in config uses it)");
  } else if (!strcmp(c, "bmpro_devices")) {
    reply["t"] = "bmpro_devices";
    if (gBmproOn) reply["list"] = serialized(gBmpro.devices());
  } else if (survey::kOn && (!strcmp(c, "bmpro_connect") || !strcmp(c, "bmpro_write"))) {
    return replyErr(reply, "Bluetooth survey build: listen-only, nothing is sent to the BMPRO");
  } else if (!strcmp(c, "bmpro_connect")) {
    if (!gBmproOn) return replyErr(reply, "BMPRO link not running");
    const char *addr = cmd["addr"] | "";
    if (strlen(addr) != 17) return replyErr(reply, "address must look like aa:bb:cc:dd:ee:ff");
    strcpy(gConfig.bmproAddr, addr);  // remembered: reconnects after reboot
    gConfig.save();
    gBmpro.connect(addr);
  } else if (!strcmp(c, "bmpro_write")) {
    // Protocol experiments: replay bytes captured from the Odyssey app.
    // {"c":"bmpro_write","uuid":"...","hex":"10 01 01","resp":true}
    if (!gBmproOn) return replyErr(reply, "BMPRO link not running");
    uint8_t buf[64];
    size_t n = 0;
    const char *h = cmd["hex"] | "";
    while (*h && n < sizeof(buf)) {
      if (!isxdigit((unsigned char)h[0])) {
        h++;
        continue;
      }
      if (!isxdigit((unsigned char)h[1])) return replyErr(reply, "hex must be pairs of digits");
      char b[3] = {h[0], h[1], 0};
      buf[n++] = (uint8_t)strtoul(b, nullptr, 16);
      h += 2;
    }
    if (!n || !gBmpro.write(cmd["uuid"] | "", buf, n, cmd["resp"] | true))
      return replyErr(reply, "write not queued");
    reply["t"] = "ok";
  } else if (!strcmp(c, "reboot")) {
    gShunt.persist(true);
    delay(200);
    ESP.restart();
  } else {
    replyErr(reply, "unknown command");
  }
}

// ---------------------------------------------------------------- periodic logic
static void updateLoads(uint32_t now) {
  for (int i = 0; i < cv::NUM_PUMPS; i++) {
    SwitchedOutput &o = gPump[i];
    const PumpCfg &pc = gConfig.pumps[i];
    cv::Pump &p = gState.pump[i];
    if (gConfig.pumpViaBmpro(i)) {
      // State comes from the BMPRO (p.sensed filled by gBmpro.loop).
      if (p.sensed && !p.on) {
        gBmproPumpSince[i] = now;
        gBmproPumpTripped[i] = false;
      }
      p.on = p.sensed;
      p.runSec = p.on ? (now - gBmproPumpSince[i]) / 1000 : 0;
      bool dry = false;
      if (pc.dryTank >= 0 && pc.dryTank < cv::NUM_TANKS) {
        const cv::Tank &t = gState.tank[pc.dryTank];
        dry = t.online && t.pct < pc.dryPct;
        if (!dry && t.pct > pc.dryPct + 3) p.lockout = false;
      }
      if (dry) p.lockout = true;
      // Same protections as the relay path, applied once per run.
      bool overRun = pc.maxRunMin && p.runSec > pc.maxRunMin * 60u;
      if (p.on && (dry || overRun) && !gBmproPumpTripped[i]) {
        gBmproPumpTripped[i] = true;
        if (switchPump(i, false, nullptr))
          gNotify.push(gConfig.meta.pumpName[i],
                       dry ? "Switched off - tank empty (dry-run protection)" : "Switched off - maximum run time reached",
                       cv::SEV_WARN);
      }
      continue;
    }
    o.loop();
    p.sensed = o.sensed();
    p.on = o.commanded() || (o.hasSense() && o.sensed());
    p.runSec = p.on ? (now - o.onSinceMs()) / 1000 : 0;

    // Dry-run protection: never run a pump on an empty tank.
    bool dry = false;
    if (pc.dryTank >= 0 && pc.dryTank < cv::NUM_TANKS) {
      const cv::Tank &t = gState.tank[pc.dryTank];
      dry = t.online && t.pct < pc.dryPct;
    }
    if (dry && !p.lockout) {
      p.lockout = true;
      if (o.commanded()) {
        o.set(false);
        gNotify.push(gConfig.meta.pumpName[i], "Switched off - tank empty (dry-run protection)",
                     cv::SEV_WARN);
      }
    } else if (!dry && p.lockout) {
      // Release with a little hysteresis once the tank has been refilled.
      bool valid = pc.dryTank >= 0 && pc.dryTank < cv::NUM_TANKS;
      if (!valid || gState.tank[pc.dryTank].pct > pc.dryPct + 3) p.lockout = false;
    }
    // Max run time (e.g. a burst hose shouldn't empty the tank onto the road).
    if (pc.maxRunMin && o.commanded() && p.runSec > pc.maxRunMin * 60u) {
      o.set(false);
      gNotify.push(gConfig.meta.pumpName[i], "Switched off - maximum run time reached", cv::SEV_WARN);
    }
  }
  heaterLoop();
  gInverter.loop();
  gState.inv.on = gInverter.isOn();
  gState.inv.acPresent = gInverter.sensed();

  // A freshly learned IR code adds/renames a preset -> tell every client.
  static bool wasLearning = false;
  gAircon.loop();
  if (wasLearning && !gAircon.learning() && gAircon.learnResult() == 1) configChanged();
  wasLearning = gAircon.learning();
}

static void buzzerLoop(uint32_t now) {
  bool alarm = gConfig.buzzer && gAlerts.anyUnackedAlarm();
  // Secure mode entry delay: a short chirp every second until it's disarmed.
  bool entry = gState.sec.mode == cv::SEC_ENTRY;
  digitalWrite(PIN_BUZZER, (alarm && (now % 2000) < 200) || (entry && (now % 1000) < 40) ? HIGH : LOW);
}

static void midnightReset() {
  static int lastDay = -1;
  if (!gNet.timeValid()) return;
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  if (lastDay != -1 && lt.tm_mday != lastDay) {
    gTemps.resetMinMax(gState);
    if (gVictronBleOn) gVictronBle.resetDaily();  // VE.Direct resets its own peak
  }
  lastDay = lt.tm_mday;
}

// Network services run on core 0 so a slow TLS handshake over Starlink never
// stalls sensor reading or the touchscreen link on core 1.
static void netTask(void *) {
  gWeb.begin();
  gCloud.begin();
  for (;;) {
    gWeb.loop(gState);
    gCloud.loop(gState);
    gWeather.loop(gState);
    {
      StateLock l;
      gState.cloud = gCloud.connected();
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ---------------------------------------------------------------- setup / loop
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Caravan Power hub ===");
  gStateMux = xSemaphoreCreateRecursiveMutex();

  pinMode(PIN_BUZZER, OUTPUT);
  if (!LittleFS.begin(true)) Serial.println("[fs] LittleFS mount failed");
  if (!gConfig.load()) {
    Serial.println("[cfg] no config, writing defaults");
    gConfig.save();
  }

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  gTemps.begin(PIN_ONEWIRE);
  if (gConfig.tankSource == TANK_ADC) gTanks.begin();
  gLevel.begin();
  secure::begin(millis());
  gShunt.begin();

  gPump[0].begin("pump1", PIN_PUMP1_OUT, PIN_PUMP1_SENSE);
  gPump[1].begin("pump2", PIN_PUMP2_OUT, PIN_PUMP2_SENSE);
  gInverter.begin("inverter", PIN_INVERTER_OUT, PIN_INVERTER_AC_SENSE);
  applyOutputConfig();
  gAircon.begin(PIN_IR_TX, PIN_IR_RX);

  gLink.begin(LINK_SERIAL, PIN_LINK_RX, PIN_LINK_TX, PIN_LINK_DE, PIN_LINK_RE);
  if (demo::kOn) demo::begin(gState);
  gNet.begin();
  gNotify.begin();
  for (int i = 0; i < cv::NUM_SOLAR; i++) startSolar(i);
  if (gConfig.victron[GPS_SHARES_SOLAR].src != SOLAR_VEDIRECT) {  // the shared UART is free for a GPS
    GPS_SERIAL.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    gGpsOn = true;
  }
  for (auto s : gConfig.tempSrc)
    if (s == TEMP_BLE) startBleTemps();
  startHeater();
  if (survey::kOn) {
    survey::begin();  // listen-only Bluetooth logging; the BMPRO link stays off
  } else if (gConfig.usesBmpro()) {
    gBmpro.begin(gConfig.bmproAddr, gConfig.bmproExplorer);
    gBmproOn = true;
  }
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);

  // Watchdog: if loop() stops running for 30 s (a hang or a deadlock), the
  // hub restarts itself. The Arduino core feeds it after every loop().
  esp_task_wdt_init(30, true);
  enableLoopWDT();
}

void loop() {
  uint32_t now = millis();
  {
    StateLock l;
    if (!demo::kOn) {
    for (int i = 0; i < cv::NUM_SOLAR; i++) {
      uint8_t src = gConfig.victron[i].src;
      if (src == SOLAR_VEDIRECT && gVeDirectOn[i]) gVictron[i].poll(gState.solar[i]);
      else if (src == SOLAR_BLE && gVictronBleOn) gVictronBle.poll(i, gState.solar[i]);
      else gState.solar[i].online = false;
    }
    gTemps.poll(gState);
    {
      float lat, lon;
      char name[24];
      const char *why = nullptr;
      if (gLevel.takeLocation(lat, lon, name, sizeof(name))) {
        if (applyLocation(lat, lon, name, cv::LOC_PHONE, &why)) configChanged();
        else Serial.printf("[loc] phone location refused: %s\n", why);
      }
    }
    gpsLoop(now);
    if (gBleTempsOn) gBleTemps.poll(gState);
    if (gConfig.tankSource == TANK_ADC) gTanks.poll(gState);
    gShunt.poll(gState, now);
    if (gBmproOn) {
      bool pumps = gConfig.pumpViaBmpro(0) || gConfig.pumpViaBmpro(1);
      gBmpro.loop(gState, gConfig.bat.source == cv::BAT_SRC_BMPRO, gConfig.tankSource == TANK_BMPRO, pumps);
    }
    }  // !demo::kOn
    // Level sensor: always read (secure mode needs it); in the fake-data build
    // its level isn't shown, and with no sensor plugged in a simulated one
    // plays out wind and then a theft after arming.
    gLevel.loop(gState, !demo::kOn);
    if (demo::kOn && !gLevel.online()) secure::simLoop(now);
    if (secure::loop(gState, secureSensorOk(), now)) stateChanged();
    updateLoads(now);
    gNet.loop(gState);
    if (demo::kOn) demo::tick(gState, now);
    // Mains chargers have no sensor: whatever charge the BMPRO and Victrons
    // don't explain is theirs. Runs every loop, after the battery drivers, so
    // chargeA / loadA always include it. Uses the BMPRO load figure when known.
    gAcCharge.update(gState, now, demo::kOn ? demo::loadA() : gBmproOn ? gBmpro.loadA() : -1);

    static uint32_t nextEval = 0;
    if ((int32_t)(now - nextEval) >= 0) {
      nextEval = now + 1000;
      gState.seq++;
      gAlerts.evaluate(gState, gConfig.rules, now, gState.ts, onAlert, nullptr);
      midnightReset();
    }
    gLink.loop(gState);
  }
  buzzerLoop(now);
  if (survey::kOn) survey::loop();
  if (gRestartAtMs && millis() - gRestartAtMs > 1000) ESP.restart();
  delay(2);
}
