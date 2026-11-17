#include "HubConfig.h"

#include <LittleFS.h>

HubConfig gConfig;

static const char *kPath = "/config.json";

void HubConfig::setDefaults() {
  *this = HubConfig();
  cv::loadDefaultRules(rules);
  for (int i = 0; i < cv::NUM_TANKS; i++) tanks[i].channel = i;
  pumps[0].dryTank = 0;
  pumps[1].dryTank = 1;
  meta.tankCal = tankSource == TANK_ADC;
}

static void copyStr(char *d, size_t n, const char *s) {
  strncpy(d, s ? s : "", n - 1);
  d[n - 1] = 0;
}

void HubConfig::toJson(JsonObject o) const {
  cv::metaToJson(meta, o["meta"].to<JsonObject>());
  cv::rulesToJson(rules, o["rules"].to<JsonArray>());

  JsonObject b = o["bat"].to<JsonObject>();
  b["src"] = bat.source;
  b["cap"] = bat.capacityAh;
  b["capSet"] = true;  // see fromJson: tells a chosen 300 Ah from the old default
  b["eff"] = bat.chargeEff;
  b["fullV"] = bat.fullV;
  b["tailA"] = bat.tailA;
  b["shunt"] = bat.shuntOhm;
  b["chg"] = bat.chargerShunt;
  b["chgShunt"] = bat.chargerShuntOhm;
  b["vCal"] = bat.vCal;

  JsonArray t = o["tanks"].to<JsonArray>();
  for (auto &x : tanks) {
    JsonObject j = t.add<JsonObject>();
    j["empty"] = x.emptyMv;
    j["full"] = x.fullMv;
    j["ch"] = x.channel;
    j["en"] = x.enabled;
  }
  JsonArray p = o["pumps"].to<JsonArray>();
  for (auto &x : pumps) {
    JsonObject j = p.add<JsonObject>();
    j["mode"] = x.mode;
    j["pulse"] = x.pulseMs;
    j["maxRun"] = x.maxRunMin;
    j["dryTank"] = x.dryTank;
    j["dryPct"] = x.dryPct;
    j["sense"] = x.useSense;
  }
  JsonArray vic = o["vic"].to<JsonArray>();
  for (auto &v : victron) {
    JsonObject j = vic.add<JsonObject>();
    j["src"] = v.src;
    j["addr"] = v.addr;
    j["key"] = v.key;
  }
  JsonObject inv = o["inv"].to<JsonObject>();
  inv["mode"] = inverter.mode;
  inv["pulse"] = inverter.pulseMs;
  inv["sense"] = inverter.useSense;

  JsonArray ta = o["tempAddr"].to<JsonArray>();
  for (auto &a : tempAddr) ta.add(a);
  JsonObject lv = o["lvl"].to<JsonObject>();
  lv["zeroed"] = level.zeroed;
  lv["fwd"] = level.cal.fwdLearned;
  JsonArray u = lv["up"].to<JsonArray>();
  u.add(level.cal.up.x);
  u.add(level.cal.up.y);
  u.add(level.cal.up.z);
  JsonArray f = lv["f"].to<JsonArray>();
  f.add(level.cal.fwd.x);
  f.add(level.cal.fwd.y);
  f.add(level.cal.fwd.z);
  lv["track"] = level.trackMm;
  lv["jockey"] = level.jockeyMm;
  JsonObject loc = o["loc"].to<JsonObject>();
  loc["src"] = location.src;
  loc["lat"] = location.lat;
  loc["lon"] = location.lon;
  loc["name"] = location.name;
  JsonObject sc = o["sec"].to<JsonObject>();
  sc["armed"] = secure.armed;
  sc["sens"] = secure.sens;
  sc["code"] = secure.code;
  if (secure.haveBase) {
    JsonArray sb = sc["base"].to<JsonArray>();
    sb.add(secure.base.x);
    sb.add(secure.base.y);
    sb.add(secure.base.z);
  }
  JsonObject ht = o["htr"].to<JsonObject>();
  ht["link"] = heater.link;
  ht["addr"] = heater.addr;
  ht["pass"] = heater.passkey;
  JsonArray ts = o["tsrc"].to<JsonArray>();
  for (auto v : tempSrc) ts.add(v);
  JsonArray tb = o["tble"].to<JsonArray>();
  for (auto &a : tempBle) tb.add(a);
  o["cloudInt"] = cloudIntervalSec;
  o["buzzer"] = buzzer;
  o["tankSrc"] = tankSource;
  o["bmproAddr"] = bmproAddr;
  o["bmproExplorer"] = bmproExplorer;
}

void HubConfig::fromJson(JsonObjectConst o) {
  if (o["meta"].is<JsonObjectConst>()) cv::metaFromJson(meta, o["meta"]);
  if (o["rules"].is<JsonArrayConst>()) cv::rulesFromJson(rules, o["rules"]);
  cv::ensureTheftRule(rules);  // rule lists saved before secure mode existed

  JsonObjectConst b = o["bat"];
  if (!b.isNull()) {
    bat.source = b["src"] | bat.source;
    bat.capacityAh = b["cap"] | bat.capacityAh;
    // 300 Ah was the built-in default before it could be set from the phone,
    // so a saved 300 means "never set": use Beau's 405 Ah bank instead.
    if (bat.capacityAh == 300 && !(b["capSet"] | false)) bat.capacityAh = cv::BAT_CAP_DEFAULT_AH;
    if (bat.capacityAh < cv::BAT_CAP_MIN_AH || bat.capacityAh > cv::BAT_CAP_MAX_AH) bat.capacityAh = cv::BAT_CAP_DEFAULT_AH;
    bat.chargeEff = b["eff"] | bat.chargeEff;
    bat.fullV = b["fullV"] | bat.fullV;
    bat.tailA = b["tailA"] | bat.tailA;
    bat.shuntOhm = b["shunt"] | bat.shuntOhm;
    bat.chargerShunt = b["chg"] | bat.chargerShunt;
    bat.chargerShuntOhm = b["chgShunt"] | bat.chargerShuntOhm;
    bat.vCal = b["vCal"] | bat.vCal;
  }
  meta.batCapacityAh = bat.capacityAh;
  meta.tankCal = tankSource == TANK_ADC;

  for (int i = 0; i < cv::NUM_TANKS; i++) {
    JsonObjectConst j = o["tanks"][i];
    if (j.isNull()) continue;
    tanks[i].emptyMv = j["empty"] | tanks[i].emptyMv;
    tanks[i].fullMv = j["full"] | tanks[i].fullMv;
    tanks[i].channel = j["ch"] | tanks[i].channel;
    tanks[i].enabled = j["en"] | tanks[i].enabled;
  }
  for (int i = 0; i < cv::NUM_PUMPS; i++) {
    JsonObjectConst j = o["pumps"][i];
    if (j.isNull()) continue;
    pumps[i].mode = (SwitchMode)(j["mode"] | (uint8_t)pumps[i].mode);
    pumps[i].pulseMs = j["pulse"] | pumps[i].pulseMs;
    pumps[i].maxRunMin = j["maxRun"] | pumps[i].maxRunMin;
    pumps[i].dryTank = j["dryTank"] | pumps[i].dryTank;
    pumps[i].dryPct = j["dryPct"] | pumps[i].dryPct;
    pumps[i].useSense = j["sense"] | pumps[i].useSense;
  }
  for (int i = 0; i < cv::NUM_SOLAR; i++) {
    JsonObjectConst j = o["vic"][i];
    if (j.isNull()) continue;
    victron[i].src = j["src"] | victron[i].src;
    if (j["addr"].is<const char *>()) copyStr(victron[i].addr, sizeof(victron[i].addr), j["addr"]);
    if (j["key"].is<const char *>()) copyStr(victron[i].key, sizeof(victron[i].key), j["key"]);
  }
  JsonObjectConst inv = o["inv"];
  if (!inv.isNull()) {
    inverter.mode = (SwitchMode)(inv["mode"] | (uint8_t)inverter.mode);
    inverter.pulseMs = inv["pulse"] | inverter.pulseMs;
    inverter.useSense = inv["sense"] | inverter.useSense;
  }
  for (int i = 0; i < cv::NUM_TEMPS; i++)
    if (o["tempAddr"][i].is<const char *>()) copyStr(tempAddr[i], sizeof(tempAddr[i]), o["tempAddr"][i]);
  JsonObjectConst loc = o["loc"];
  if (!loc.isNull()) {
    location.src = loc["src"] | location.src;
    location.lat = loc["lat"] | location.lat;
    location.lon = loc["lon"] | location.lon;
    if (loc["name"].is<const char *>()) copyStr(location.name, sizeof(location.name), loc["name"]);
  }
  meta.locSrc = location.src;
  JsonObjectConst sc = o["sec"];
  if (!sc.isNull()) {
    secure.armed = sc["armed"] | false;
    secure.sens = sc["sens"] | secure.sens;
    if (sc["code"].is<const char *>()) copyStr(secure.code, sizeof(secure.code), sc["code"]);
    if (secure.sens > cv::SENS_HIGH) secure.sens = cv::SENS_MEDIUM;
    secure.haveBase = sc["base"].size() == 3;
    if (secure.haveBase) secure.base = {sc["base"][0].as<float>(), sc["base"][1].as<float>(), sc["base"][2].as<float>()};
  }
  meta.secCodeSet = secure.code[0] != 0;
  JsonObjectConst ht = o["htr"];
  if (!ht.isNull()) {
    heater.link = ht["link"] | heater.link;
    if (heater.link > cv::HTR_LINK_RF) heater.link = cv::HTR_LINK_NONE;
    if (ht["addr"].is<const char *>()) copyStr(heater.addr, sizeof(heater.addr), ht["addr"]);
    heater.passkey = ht["pass"] | heater.passkey;
  }
  copyStr(meta.locName, sizeof(meta.locName), location.name);
  JsonObjectConst lv = o["lvl"];
  if (!lv.isNull()) {
    level.zeroed = lv["zeroed"] | false;
    level.cal.zeroed = level.zeroed;
    level.cal.fwdLearned = lv["fwd"] | false;
    if (lv["up"].size() == 3)
      level.cal.up = {lv["up"][0].as<float>(), lv["up"][1].as<float>(), lv["up"][2].as<float>()};
    if (lv["f"].size() == 3)
      level.cal.fwd = {lv["f"][0].as<float>(), lv["f"][1].as<float>(), lv["f"][2].as<float>()};
    level.trackMm = lv["track"] | level.trackMm;
    level.jockeyMm = lv["jockey"] | level.jockeyMm;
  }
  for (int i = 0; i < cv::NUM_TEMPS; i++) {
    tempSrc[i] = o["tsrc"][i] | tempSrc[i];
    if (o["tble"][i].is<const char *>()) copyStr(tempBle[i], sizeof(tempBle[i]), o["tble"][i]);
  }
  cloudIntervalSec = o["cloudInt"] | cloudIntervalSec;
  if (cloudIntervalSec < 5) cloudIntervalSec = 5;
  buzzer = o["buzzer"] | buzzer;
  tankSource = o["tankSrc"] | tankSource;
  if (o["bmproAddr"].is<const char *>()) copyStr(bmproAddr, sizeof(bmproAddr), o["bmproAddr"]);
  bmproExplorer = o["bmproExplorer"] | bmproExplorer;
}

bool HubConfig::load() {
  setDefaults();
  File f = LittleFS.open(kPath, "r");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("[cfg] parse error: %s - using defaults\n", err.c_str());
    return false;
  }
  fromJson(doc.as<JsonObjectConst>());
  return true;
}

bool HubConfig::save() const {
  JsonDocument doc;
  toJson(doc.to<JsonObject>());
  // Write to a temp file then rename, so a brown-out can't corrupt config.
  File f = LittleFS.open("/config.tmp", "w");
  if (!f) return false;
  size_t n = serializeJson(doc, f);
  f.close();
  if (n == 0) return false;
  LittleFS.remove(kPath);
  return LittleFS.rename("/config.tmp", kPath);
}
