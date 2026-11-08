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
  JsonObject inv = o["inv"].to<JsonObject>();
  inv["mode"] = inverter.mode;
  inv["pulse"] = inverter.pulseMs;
  inv["sense"] = inverter.useSense;

  JsonArray ta = o["tempAddr"].to<JsonArray>();
  for (auto &a : tempAddr) ta.add(a);
  o["cloudInt"] = cloudIntervalSec;
  o["buzzer"] = buzzer;
  o["bmproExplorer"] = bmproExplorer;
}

void HubConfig::fromJson(JsonObjectConst o) {
  if (o["meta"].is<JsonObjectConst>()) cv::metaFromJson(meta, o["meta"]);
  if (o["rules"].is<JsonArrayConst>()) cv::rulesFromJson(rules, o["rules"]);

  JsonObjectConst b = o["bat"];
  if (!b.isNull()) {
    bat.source = b["src"] | bat.source;
    bat.capacityAh = b["cap"] | bat.capacityAh;
    bat.chargeEff = b["eff"] | bat.chargeEff;
    bat.fullV = b["fullV"] | bat.fullV;
    bat.tailA = b["tailA"] | bat.tailA;
    bat.shuntOhm = b["shunt"] | bat.shuntOhm;
    bat.chargerShunt = b["chg"] | bat.chargerShunt;
    bat.chargerShuntOhm = b["chgShunt"] | bat.chargerShuntOhm;
    bat.vCal = b["vCal"] | bat.vCal;
  }
  meta.batCapacityAh = bat.capacityAh;

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
  JsonObjectConst inv = o["inv"];
  if (!inv.isNull()) {
    inverter.mode = (SwitchMode)(inv["mode"] | (uint8_t)inverter.mode);
    inverter.pulseMs = inv["pulse"] | inverter.pulseMs;
    inverter.useSense = inv["sense"] | inverter.useSense;
  }
  for (int i = 0; i < cv::NUM_TEMPS; i++)
    if (o["tempAddr"][i].is<const char *>()) copyStr(tempAddr[i], sizeof(tempAddr[i]), o["tempAddr"][i]);
  cloudIntervalSec = o["cloudInt"] | cloudIntervalSec;
  if (cloudIntervalSec < 5) cloudIntervalSec = 5;
  buzzer = o["buzzer"] | buzzer;
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
