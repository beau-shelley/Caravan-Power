// Caravan Power - HUB firmware
// Reads every system in the van, runs the alert rules, drives the pumps /
// inverter / aircon, and serves the data to the 7" display (RS485), the local
// web dashboard (Wi-Fi) and the cloud (MQTT over TLS) with phone push alerts.
#include <Arduino.h>
#include <cctype>
#include <LittleFS.h>
#include <Wire.h>

#include "AlertEngine.h"
#include "Bmpro.h"
#include "CaravanModel.h"
#include "Comms.h"
#include "HubConfig.h"
#include "Inputs.h"
#include "Outputs.h"
#include "VictronBle.h"
#include "BleTempRx.h"
#include "LevelSensor.h"
#include "WeatherClient.h"
#include "VictronBleRx.h"
#include "pins.h"

using namespace hub;

static cv::State gState;
static cv::AlertEngine gAlerts;
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

// GPS fixes update the weather location (saved only after moving > 5 km, so
// the last known spot survives a reboot without wearing out the flash).
static void gpsLoop(uint32_t now) {
  if (!gGpsOn) return;
  while (Serial2.available()) {
    if (!gGps.feed((uint8_t)Serial2.read())) continue;
    gGpsFixMs = now;
    LocationCfg &loc = gConfig.location;
    float lat = (float)gGps.lat(), lon = (float)gGps.lon();
    if (loc.src != cv::LOC_GPS || cv::distanceKm(lat, lon, loc.lat, loc.lon) > 5) {
      loc.src = cv::LOC_GPS;
      loc.lat = lat;
      loc.lon = lon;
      loc.name[0] = 0;
      gConfig.save();
      gWeather.refreshSoon();
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
    if (i == 0) gVictron[0].begin(Serial1, PIN_VE1_RX, PIN_VE1_TX);
    else {
      gGpsOn = false;  // UART2 is shared with the optional GPS
      gVictron[1].begin(Serial2, PIN_VE2_RX, PIN_VE2_TX);
    }
    gVeDirectOn[i] = true;
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

// ---------------------------------------------------------------- commands
// One handler for every client (touchscreen, local web, cloud). Messages look
// like {"c":"pump","i":0,"on":true}. See docs/PROTOCOL.md for the full list.
static bool isMutating(const char *c) {
  static const char *readOnly[] = {"state", "hello", "rules_get", "sensors", "bmpro_log", "victron_seen", "ble_sensors", "level_status",
                                   "bmpro_devices", "ac_learn_status", "ping"};
  for (auto r : readOnly)
    if (strcmp(c, r) == 0) return false;
  return true;
}

void hub::handleCommand(JsonObjectConst cmd, Source src, JsonDocument &reply) {
  StateLock lock;
  const char *c = cmd["c"] | "";

  if (src == SRC_CLOUD && isMutating(c) && !gConfig.meta.remoteControl) {
    replyErr(reply, "Remote control is disabled on the caravan display");
    return;
  }

  if (!strcmp(c, "ping")) {
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
  } else if (!strcmp(c, "config_set")) {
    bool rc = gConfig.meta.remoteControl;
    gConfig.fromJson(cmd["cfg"]);
    if (src == SRC_CLOUD) gConfig.meta.remoteControl = rc;
    applyOutputConfig();
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
    float lat = cmd["lat"] | 999.0f, lon = cmd["lon"] | 999.0f;
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return replyErr(reply, "bad coordinates");
    if (gGpsOn && gGpsFixMs && millis() - gGpsFixMs < 600000)
      return replyErr(reply, "The GPS has a fix - it sets the location automatically");
    LocationCfg &loc = gConfig.location;
    loc.src = cmd["src"] | (uint8_t)cv::LOC_MANUAL;
    loc.lat = lat;
    loc.lon = lon;
    snprintf(loc.name, sizeof(loc.name), "%s", cmd["name"] | "");
    gConfig.save();
    gWeather.refreshSoon();
    reply["t"] = "ok";
  } else if (!strcmp(c, "weather_refresh")) {
    gWeather.refreshSoon();

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
  digitalWrite(PIN_BUZZER, alarm && (now % 2000) < 200 ? HIGH : LOW);
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
  gShunt.begin();

  gPump[0].begin("pump1", PIN_PUMP1_OUT, PIN_PUMP1_SENSE);
  gPump[1].begin("pump2", PIN_PUMP2_OUT, PIN_PUMP2_SENSE);
  gInverter.begin("inverter", PIN_INVERTER_OUT, PIN_INVERTER_AC_SENSE);
  applyOutputConfig();
  gAircon.begin(PIN_IR_TX, PIN_IR_RX);

  gLink.begin(Serial0, PIN_LINK_RX, PIN_LINK_TX, PIN_LINK_DE);
  gNet.begin();
  gNotify.begin();
  for (int i = 0; i < cv::NUM_SOLAR; i++) startSolar(i);
  if (gConfig.victron[1].src != SOLAR_VEDIRECT) {  // UART2 is free for a GPS
    Serial2.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    gGpsOn = true;
  }
  for (auto s : gConfig.tempSrc)
    if (s == TEMP_BLE) startBleTemps();
  if (gConfig.usesBmpro()) {
    gBmpro.begin(gConfig.bmproAddr, gConfig.bmproExplorer);
    gBmproOn = true;
  }
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
}

void loop() {
  uint32_t now = millis();
  {
    StateLock l;
    for (int i = 0; i < cv::NUM_SOLAR; i++) {
      uint8_t src = gConfig.victron[i].src;
      if (src == SOLAR_VEDIRECT && gVeDirectOn[i]) gVictron[i].poll(gState.solar[i]);
      else if (src == SOLAR_BLE && gVictronBleOn) gVictronBle.poll(i, gState.solar[i]);
      else gState.solar[i].online = false;
    }
    gTemps.poll(gState);
    gLevel.loop(gState);
    gpsLoop(now);
    if (gBleTempsOn) gBleTemps.poll(gState);
    if (gConfig.tankSource == TANK_ADC) gTanks.poll(gState);
    gShunt.poll(gState, now);
    if (gBmproOn) {
      bool pumps = gConfig.pumpViaBmpro(0) || gConfig.pumpViaBmpro(1);
      gBmpro.loop(gState, gConfig.bat.source == cv::BAT_SRC_BMPRO, gConfig.tankSource == TANK_BMPRO, pumps);
    }
    updateLoads(now);
    gNet.loop(gState);

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
  delay(2);
}
