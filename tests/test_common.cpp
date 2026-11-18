// Host-side unit tests for the hardware-independent firmware logic.
// Build & run:  make -C tests   (see tests/Makefile)
#include <cmath>
#include <cstdio>
#include <string>

#include "AcCharge.h"
#include "AirHeater.h"
#include "AlertEngine.h"
#include "CaravanModel.h"
#include "LinkCodec.h"
#include "SocEstimator.h"
#include "VeDirect.h"
#include "VictronBle.h"
#include "BleSensors.h"
#include <vector>
#include "BleTag.h"
#include "BmproAdv.h"
#include "BmproLink.h"
#include "Leveling.h"
#include "TheftGuard.h"
#include "Weather.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                 \
  do {                                                              \
    checks++;                                                       \
    if (!(cond)) {                                                  \
      failures++;                                                   \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
    }                                                               \
  } while (0)
#define NEAR(a, b, eps) CHECK(std::fabs((a) - (b)) < (eps))

using namespace cv;

// Build a VE.Direct block with a correct checksum byte.
static std::string veBlock(const std::string &body) {
  std::string s = body + "\r\nChecksum\t";
  uint8_t sum = 0;
  for (unsigned char c : s) sum += c;
  s.push_back((char)(uint8_t)(256 - sum));
  return s;
}

static void testVeDirect() {
  VeDirectParser p;
  std::string blk = veBlock(
      "\r\nPID\t0xA057\r\nFW\t159\r\nSER#\tHQ2132ABCDE\r\nV\t13270\r\nI\t21400\r\nVPV\t38120"
      "\r\nPPV\t298\r\nCS\t3\r\nMPPT\t2\r\nERR\t0\r\nLOAD\tON\r\nIL\t0\r\nH19\t12345"
      "\r\nH20\t87\r\nH21\t412\r\nH22\t140\r\nH23\t455\r\nHSDS\t210");
  int commits = 0;
  // Leading garbage + a HEX frame in the middle must be tolerated.
  std::string stream = "xx\r\n" + blk.substr(0, 40) + ":A0102000543\n" + blk.substr(40) + blk;
  for (unsigned char c : stream) commits += p.feed(c);
  CHECK(commits >= 1);
  Solar s;
  p.applyTo(s);
  NEAR(s.batV, 13.27f, 0.001f);
  NEAR(s.batA, 21.4f, 0.001f);
  NEAR(s.pvV, 38.12f, 0.001f);
  NEAR(s.pvW, 298.f, 0.01f);
  CHECK(s.cs == 3);
  NEAR(s.yieldTodayKwh, 0.87f, 0.001f);
  CHECK(s.maxWToday == 412);
  CHECK(std::string(p.get("SER#")) == "HQ2132ABCDE");

  // Corrupted block must be rejected.
  VeDirectParser q;
  std::string bad = blk;
  bad[20] ^= 0x01;
  int ok = 0;
  for (unsigned char c : bad) ok += q.feed(c);
  CHECK(ok == 0);
  CHECK(q.badBlocks() == 1);
}

static void testAlerts() {
  State s;
  s.bat.online = true;
  s.bat.source = BAT_SRC_SHUNT;
  s.bat.v = 12.8f;
  RuleSet rs;
  Rule r;
  strcpy(r.name, "Battery low");
  r.metric = M_BAT_V;
  r.cmp = CMP_LT;
  r.threshold = 12.4f;
  r.hysteresis = 0.3f;
  r.delaySec = 10;
  r.sev = SEV_ALARM;
  rs.upsert(r);

  AlertEngine e;
  int raised = 0, cleared = 0;
  auto cb = [](const AlertEngine::Event &ev, void *ctx) {
    int *c = (int *)ctx;
    if (ev.type == AlertEngine::RAISED) c[0]++;
    else c[1]++;
  };
  int counts[2] = {0, 0};
  e.evaluate(s, rs, 0, 1000, cb, counts);
  CHECK(s.alertCount == 0);
  s.bat.v = 12.2f;
  e.evaluate(s, rs, 1000, 1001, cb, counts);
  CHECK(s.alertCount == 0);  // still within delay
  e.evaluate(s, rs, 12000, 1012, cb, counts);
  CHECK(s.alertCount == 1);
  CHECK(counts[0] == 1);
  CHECK(e.anyUnackedAlarm());
  CHECK(strstr(s.alerts[0].msg, "12.20V") != nullptr);
  e.ackAll(s);
  CHECK(!e.anyUnackedAlarm());
  s.bat.v = 12.5f;  // above threshold but inside hysteresis band
  e.evaluate(s, rs, 13000, 1013, cb, counts);
  CHECK(s.alertCount == 1);
  CHECK(s.alerts[0].acked);
  s.bat.v = 12.75f;
  e.evaluate(s, rs, 14000, 1014, cb, counts);
  CHECK(s.alertCount == 0);
  CHECK(counts[1] == 1);

  // Glitch shorter than the delay never fires.
  s.bat.v = 12.0f;
  e.evaluate(s, rs, 20000, 0, cb, counts);
  s.bat.v = 12.9f;
  e.evaluate(s, rs, 25000, 0, cb, counts);
  s.bat.v = 12.0f;
  e.evaluate(s, rs, 29000, 0, cb, counts);
  CHECK(s.alertCount == 0);
  (void)raised;
  (void)cleared;

  // Offline sensors don't raise.
  RuleSet defs;
  loadDefaultRules(defs);
  CHECK(defs.count >= 10);
  State off;
  AlertEngine e2;
  e2.evaluate(off, defs, 0, 0);
  e2.evaluate(off, defs, 5000000, 0);
  CHECK(off.alertCount == 0);

  // Delete rule clears its alert
  rs.rules[0].delaySec = 0;
  s.bat.v = 11.0f;
  e.evaluate(s, rs, 40000, 0, cb, counts);
  CHECK(s.alertCount == 1);
  rs.remove(rs.rules[0].id);
  e.evaluate(s, rs, 41000, 0, cb, counts);
  CHECK(s.alertCount == 0);
}

// Stepped rule: 30 % warning -> 15 % alarm -> 5 % critical. Each step fires
// once; drifting around inside a step doesn't re-fire; recovering re-arms.
static void testAlertLevels() {
  State s;
  s.bat.online = true;
  s.bat.source = BAT_SRC_SHUNT;
  s.bat.soc = 50;
  RuleSet rs;
  Rule r;
  strcpy(r.name, "Battery charge low");
  r.metric = M_BAT_SOC;
  r.cmp = CMP_LT;
  r.threshold = 30;
  r.hysteresis = 5;
  r.delaySec = 0;
  r.sev = SEV_WARN;
  r.action = ACT_PUMPS_OFF;
  r.levelCount = 2;
  r.levels[0] = {15, SEV_ALARM};
  r.levels[1] = {5, SEV_CRIT};
  rs.upsert(r);

  struct Log { int raised = 0, cleared = 0; uint8_t lastSev = 0; float lastThr = 0; Action lastAct = ACT_NONE; } log;
  auto cb = [](const AlertEngine::Event &ev, void *ctx) {
    Log *l = (Log *)ctx;
    if (ev.type == AlertEngine::RAISED) {
      l->raised++;
      l->lastSev = ev.rule.sev;
      l->lastThr = ev.rule.threshold;
      l->lastAct = ev.rule.action;
    } else {
      l->cleared++;
    }
  };
  AlertEngine e;
  uint32_t t = 0;
  auto step = [&](float soc) { s.bat.soc = soc; e.evaluate(s, rs, t += 1000, 0, cb, &log); };

  step(50);
  CHECK(log.raised == 0);
  step(29);
  CHECK(log.raised == 1 && log.lastSev == SEV_WARN && log.lastAct == ACT_NONE);
  step(28); step(31); step(29); step(20);  // inside the band: no repeat
  CHECK(log.raised == 1);
  step(14);
  CHECK(log.raised == 2 && log.lastSev == SEV_ALARM && log.lastThr == 15);
  CHECK(s.alertCount == 1 && s.alerts[0].sev == SEV_ALARM && !s.alerts[0].acked);
  CHECK(strstr(s.alerts[0].msg, "< 15") != nullptr);
  step(12); step(16);
  CHECK(log.raised == 2);
  step(4);
  CHECK(log.raised == 3 && log.lastSev == SEV_CRIT && log.lastAct == ACT_PUMPS_OFF);
  CHECK(e.anyUnackedAlarm());
  step(8);  // recovered past 5 + 5? no: still critical
  CHECK(s.alerts[0].sev == SEV_CRIT);
  step(11); // >= 5 + 5: drops back to the alarm step, silently
  CHECK(log.raised == 3 && s.alerts[0].sev == SEV_ALARM && log.cleared == 0);
  step(4);  // falls again: critical re-fires
  CHECK(log.raised == 4 && log.lastSev == SEV_CRIT);
  step(36); // recovered past everything: one CLEARED
  CHECK(log.cleared == 1 && s.alertCount == 0);

  // Dropping straight past several steps fires once, at the deepest.
  step(3);
  CHECK(log.raised == 5 && log.lastSev == SEV_CRIT);

  // JSON round trip keeps the levels; invalid (not deeper) levels are dropped.
  JsonDocument d;
  ruleToJson(rs.rules[0], d.to<JsonObject>());
  CHECK(d["lv"].size() == 2 && d["lv"][1]["sev"] == 3);
  Rule back;
  CHECK(ruleFromJson(back, d.as<JsonObjectConst>()));
  CHECK(back.levelCount == 2 && back.levels[0].threshold == 15 && back.levels[1].sev == SEV_CRIT);
  d["lv"][1]["thr"] = 20;  // shallower than 15: dropped
  ruleFromJson(back, d.as<JsonObjectConst>());
  CHECK(back.levelCount == 1);
  JsonDocument plain;
  ruleToJson(Rule(), plain.to<JsonObject>());
  CHECK(!plain["lv"].is<JsonArrayConst>());  // no levels: old format unchanged

  RuleSet defs;
  loadDefaultRules(defs);
  bool found = false;
  for (int i = 0; i < defs.count; i++)
    if (defs.rules[i].metric == M_BAT_SOC) found = defs.rules[i].levelCount == 2;
  CHECK(found);
}

static void testJsonRoundTrip() {
  State s;
  s.ts = 1762560000;
  s.bat.online = true;
  s.bat.source = BAT_SRC_SHUNT;
  s.bat.v = 13.284f;
  s.bat.a = -12.34f;
  s.bat.soc = 76.5f;
  s.bat.loadA = 30.1f;
  s.solar[1].online = true;
  s.solar[1].pvW = 311;
  s.temp[TEMP_FRIDGE].online = true;
  s.temp[TEMP_FRIDGE].c = 3.46f;
  s.tank[2].online = true;
  s.tank[2].pct = 64;
  s.pump[1].on = true;
  s.ac.preset = 2;
  s.alertCount = 1;
  strcpy(s.alerts[0].msg, "Fridge warm: 7.0C (> 6.0)");
  s.alerts[0].sev = SEV_ALARM;
  strcpy(s.ip, "192.168.1.50");

  JsonDocument doc;
  stateToJson(s, doc.to<JsonObject>());
  std::string out;
  serializeJson(doc, out);
  CHECK(out.size() < 1800);
  printf("  state frame = %zu bytes\n", out.size());

  JsonDocument in;
  CHECK(deserializeJson(in, out) == DeserializationError::Ok);
  State t;
  stateFromJson(t, in.as<JsonObjectConst>());
  CHECK(t.ts == s.ts);
  NEAR(t.bat.v, 13.28f, 0.001f);
  NEAR(t.bat.a, -12.3f, 0.001f);
  CHECK(t.solar[1].online && t.solar[1].pvW == 311);
  NEAR(t.temp[TEMP_FRIDGE].c, 3.5f, 0.001f);
  CHECK(t.pump[1].on && !t.pump[0].on);
  CHECK(t.ac.preset == 2);
  CHECK(t.alertCount == 1 && t.alerts[0].sev == SEV_ALARM);
  CHECK(std::string(t.ip) == "192.168.1.50");

  RuleSet rs, rs2;
  loadDefaultRules(rs);
  JsonDocument rd;
  rulesToJson(rs, rd.to<JsonArray>());
  std::string rj;
  serializeJson(rd, rj);
  JsonDocument rd2;
  deserializeJson(rd2, rj);
  rulesFromJson(rs2, rd2.as<JsonArrayConst>());
  CHECK(rs2.count == rs.count);
  CHECK(rs2.rules[1].action == ACT_INVERTER_OFF);
  CHECK(rs2.rules[5].metric == M_TEMP_FRIDGE && rs2.rules[5].delaySec == 1200);
  CHECK(rs2.rules[3].levelCount == 2 && rs2.rules[3].levels[1].sev == SEV_CRIT);
  CHECK(strcmp(rs2.rules[0].name, rs.rules[0].name) == 0);

  Meta m;
  m.acPresetCount = 2;
  strcpy(m.acPresetName[0], "Cool 22");
  strcpy(m.acPresetName[1], "Heat 20");
  m.screenSleepSec = 0;  // "never" must survive the round trip
  JsonDocument md;
  metaToJson(m, md.to<JsonObject>());
  Meta m2;
  metaFromJson(m2, md.as<JsonObjectConst>());
  CHECK(m2.acPresetCount == 2 && strcmp(m2.acPresetName[1], "Heat 20") == 0);
  CHECK(strcmp(m2.tankName[2], "Grey") == 0);
  CHECK(m2.screenSleepSec == 0);
  Meta m3;  // an older hub that doesn't send "ss" keeps the default
  JsonDocument old;
  deserializeJson(old, R"({"t":"meta","rc":true})");
  metaFromJson(m3, old.as<JsonObjectConst>());
  CHECK(m3.screenSleepSec == 300);
}

static void testSoc() {
  SocEstimator e;
  SocEstimator::Config c;
  c.capacityAh = 100;
  c.chargeEff = 1.0f;
  e.begin(c, 50);
  uint32_t t = 0;
  e.update(13.0f, -10, t);
  for (int i = 0; i < 3600; i++) e.update(13.0f, -10, t += 1000);  // 1 h @ 10 A
  NEAR(e.soc(), 40.0f, 0.05f);
  CHECK(e.timeToGoMin(-10) == 240);
  // Full sync: high voltage + tail current held for 2 minutes
  for (int i = 0; i < 130; i++) e.update(14.0f, 2.0f, t += 1000);
  NEAR(e.soc(), 100.0f, 0.01f);
  CHECK(e.synced());
  NEAR(SocEstimator::socFromRestVoltageLfp(13.1f), 50.0f, 0.01f);
  CHECK(SocEstimator::socFromRestVoltageLfp(9.0f) == 0);
}

static void testLink() {
  const char *msg = "{\"t\":\"state\",\"ts\":1}";
  char tr[5];
  linkTrailer(msg, strlen(msg), tr);
  std::string frame = std::string("noise\n") + msg + tr;
  LinkReader r;
  int got = 0;
  for (unsigned char c : frame)
    if (r.feed(c)) {
      got++;
      CHECK(std::string(r.line()) == msg);
    }
  CHECK(got == 1);
  CHECK(r.errors() == 1);
  std::string bad = std::string(msg) + tr;
  bad[3] = 'X';
  for (unsigned char c : bad) CHECK(!r.feed(c));
}

static void testVictronBle() {
  // FIPS-197 appendix C.1
  uint8_t k[16], pt[16], ct[16];
  for (int i = 0; i < 16; i++) {
    k[i] = (uint8_t)i;
    pt[i] = (uint8_t)(i * 0x11);
  }
  aes128Encrypt(k, pt, ct);
  const uint8_t want[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                            0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
  CHECK(memcmp(ct, want, 16) == 0);

  // Advert built independently with Python 'cryptography' (AES-CTR, counter =
  // nonce as 128-bit little-endian): solar record cs=3, 13.27 V, 21.4 A,
  // 0.87 kWh today, 298 W.
  const uint8_t mfg[] = {0xE1, 0x02, 0x10, 0x60, 0xA0, 0xA0, 0x01, 0x2B, 0x1A, 0x0D, 0xD2,
                         0xB7, 0x57, 0x2D, 0xA9, 0xA0, 0x05, 0x98, 0xAD, 0xFE, 0x5D, 0x96};
  uint8_t key[16];
  CHECK(victronKeyFromHex("0df4d039 5b7d1a87 6c0c33ec b9e70dcd", key));
  CHECK(!victronKeyFromHex("0df4", key));
  CHECK(victronKeyFromHex("0DF4D0395B7D1A876C0C33ECB9E70DCD", key));
  VictronAdvert adv;
  CHECK(victronParseAdvert(mfg, sizeof(mfg), adv));
  CHECK(adv.record == VREC_SOLAR_CHARGER && adv.nonce == 0x1A2B && adv.model == 0xA060);
  uint8_t dec[32];
  size_t n = 0;
  CHECK(victronDecrypt(adv, key, dec, n));
  CHECK(n == 12);
  Solar s;
  CHECK(victronDecodeSolar(dec, n, s));
  CHECK(s.cs == 3 && s.err == 0);
  NEAR(s.batV, 13.27f, 0.001f);
  NEAR(s.batA, 21.4f, 0.001f);
  NEAR(s.yieldTodayKwh, 0.87f, 0.001f);
  NEAR(s.pvW, 298.f, 0.01f);
  CHECK(s.maxWToday == 298);
  CHECK(s.pvV < 0);

  // Wrong key is rejected by the key-check byte; other companies ignored.
  uint8_t bad[16] = {0x11};
  CHECK(!victronDecrypt(adv, bad, dec, n));
  uint8_t other[sizeof(mfg)];
  memcpy(other, mfg, sizeof(mfg));
  other[0] = 0x4C;
  CHECK(!victronParseAdvert(other, sizeof(other), adv));
}

static void testBleSensors() {
  BleSensorReading r;
  // BTHome v2 (Shelly BLU H&T style): info 0x44 (v2, unencrypted, trigger),
  // packet id, battery 87 %, temp 0x45 = -3.5 C (0.1), humidity 0x2E = 61 %.
  const uint8_t bt[] = {0x44, 0x00, 0x2A, 0x01, 87, 0x45, 0xDD, 0xFF, 0x2E, 61};
  CHECK(decodeBleServiceData(0xFCD2, bt, sizeof(bt), r));
  NEAR(r.tempC, -3.5f, 0.001f);
  NEAR(r.humidity, 61.f, 0.01f);
  CHECK(r.hasBatt && r.battPct == 87);
  // BTHome 0x02 temp (0.01) + 0x03 humidity (0.01), as pvvx firmware sends
  const uint8_t bt2[] = {0x40, 0x00, 0x10, 0x02, 0x2A, 0x09, 0x03, 0xBF, 0x13};
  CHECK(decodeBleServiceData(0xFCD2, bt2, sizeof(bt2), r));
  NEAR(r.tempC, 23.46f, 0.001f);
  NEAR(r.humidity, 50.55f, 0.001f);
  // Encrypted BTHome is rejected
  const uint8_t bt3[] = {0x41, 0x02, 0x2A, 0x09};
  CHECK(!decodeBleServiceData(0xFCD2, bt3, sizeof(bt3), r));
  // pvvx custom: mac[6], temp 3.40 C, hum 81.20 %, 2950 mV, 76 %, count, flags
  const uint8_t pv[] = {1, 2, 3, 4, 5, 6, 0x54, 0x01, 0xB8, 0x1F, 0x86, 0x0B, 76, 9, 0};
  CHECK(decodeBleServiceData(0x181A, pv, sizeof(pv), r));
  NEAR(r.tempC, 3.40f, 0.001f);
  NEAR(r.humidity, 81.2f, 0.001f);
  CHECK(r.battPct == 76);
  // ATC1441: big-endian -1.2 C
  const uint8_t atc[] = {6, 5, 4, 3, 2, 1, 0xFF, 0xF4, 70, 90, 0x0B, 0x86, 1};
  CHECK(decodeBleServiceData(0x181A, atc, sizeof(atc), r));
  NEAR(r.tempC, -1.2f, 0.001f);
  // Govee H5075: 21.7 C / 45.3 % -> packed 217453 = 0x03516D, battery 100
  const uint8_t gv[] = {0x88, 0xEC, 0x00, 0x03, 0x51, 0x6D, 100, 0x00};
  CHECK(decodeBleManufacturerData("GVH5075_1234", gv, sizeof(gv), r));
  NEAR(r.tempC, 21.7f, 0.001f);
  NEAR(r.humidity, 45.3f, 0.001f);
  // Govee negative: -5.2 C / 60.0 % -> 52600 | 0x800000
  const uint8_t gvn[] = {0x88, 0xEC, 0x00, 0x80, 0xCD, 0x78, 55, 0x00};
  CHECK(decodeBleManufacturerData("", gvn, sizeof(gvn), r));
  NEAR(r.tempC, -5.2f, 0.001f);
  NEAR(r.humidity, 60.0f, 0.001f);
  // ThermoPro TP357: flag, 4.3 C (0x002B), 72 %
  const uint8_t tp[] = {0xC2, 0x2B, 0x00, 72, 0x2C, 0x02};
  CHECK(decodeBleManufacturerData("TP357 (2B41)", tp, sizeof(tp), r));
  NEAR(r.tempC, 4.3f, 0.001f);
  CHECK(!decodeBleManufacturerData("Phone", tp, sizeof(tp), r));
}

// Rotation matrix helpers for the levelling test.
struct Mat3 {
  float m[3][3];
};
static Mat3 rotXYZ(float ax, float ay, float az) {
  float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay), cz = cosf(az), sz = sinf(az);
  // R = Rz * Ry * Rx
  return {{{cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx},
           {sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx},
           {-sy, cy * sx, cy * cx}}};
}
static Vec3 mul(const Mat3 &R, Vec3 v) {
  return {R.m[0][0] * v.x + R.m[0][1] * v.y + R.m[0][2] * v.z,
          R.m[1][0] * v.x + R.m[1][1] * v.y + R.m[1][2] * v.z,
          R.m[2][0] * v.x + R.m[2][1] * v.y + R.m[2][2] * v.z};
}
// What the accelerometer reads (1 g, pointing up) for a van whose slope is
// pitch/roll degrees, with the sensor mounted at rotation M.
static Vec3 reading(const Mat3 &M, float pitchDeg, float rollDeg) {
  const float d2r = 3.14159265f / 180.f;
  Vec3 upBody{tanf(pitchDeg * d2r), tanf(rollDeg * d2r), 1};
  float l = sqrtf(upBody.x * upBody.x + upBody.y * upBody.y + upBody.z * upBody.z);
  upBody = {upBody.x / l, upBody.y / l, upBody.z / l};
  Vec3 r = mul(M, upBody);
  return {r.x * 16384, r.y * 16384, r.z * 16384};  // raw LSB scale doesn't matter
}

static void testLeveling() {
  // 1) Sensor mounted almost square (3 deg / -2 deg tilt, 4 deg yaw): zero only.
  Mat3 M = rotXYZ(0.052f, -0.035f, 0.07f);
  LevelCal cal;
  levelSetZero(cal, reading(M, 0, 0));
  CHECK(cal.zeroed && !cal.fwdLearned);
  LevelAngles a = levelCompute(cal, reading(M, 0, 0));
  NEAR(a.pitchDeg, 0, 0.01f);
  NEAR(a.rollDeg, 0, 0.01f);
  a = levelCompute(cal, reading(M, 1.5f, -2.0f));
  NEAR(a.pitchDeg, 1.5f, 0.15f);  // small yaw error only, before learning front
  NEAR(a.rollDeg, -2.0f, 0.15f);

  // 2) Sensor mounted sideways and upside-down-ish: needs "learn front".
  Mat3 W = rotXYZ(2.6f, 0.4f, -1.9f);
  LevelCal c2;
  levelSetZero(c2, reading(W, 0, 0));
  CHECK(!levelLearnFront(c2, reading(W, 0.2f, 0)));  // too small a lift
  CHECK(levelLearnFront(c2, reading(W, 2.5f, 0)));
  for (float p : {-4.0f, 0.0f, 0.7f, 3.0f})
    for (float r : {-3.0f, 0.0f, 1.2f}) {
      a = levelCompute(c2, reading(W, p, r));
      NEAR(a.pitchDeg, p, 0.01f);
      NEAR(a.rollDeg, r, 0.01f);
    }
  // Re-zeroing keeps the learned forward direction.
  levelSetZero(c2, reading(W, 0, 0));
  CHECK(c2.fwdLearned);
  a = levelCompute(c2, reading(W, 2.0f, -1.0f));
  NEAR(a.pitchDeg, 2.0f, 0.01f);
  NEAR(a.rollDeg, -1.0f, 0.01f);

  // 3) Advice: left side 1 deg low on a 1900 mm track -> raise left ~33 mm;
  //    nose 0.5 deg high with 4500 mm axle-to-jockey -> lower jockey ~39 mm.
  LevelAngles t;
  t.rollDeg = -1.0f;
  t.pitchDeg = 0.5f;
  LevelAdvice adv = levelAdvice(t, 1900, 4500);
  CHECK(adv.raiseLeft);
  NEAR(adv.wheelMm, 33.2f, 0.2f);
  NEAR(adv.jockeyMm, -39.3f, 0.2f);
}

static void testWeather() {
  // Shape of a real Open-Meteo response (trimmed units blocks).
  const char *json = R"({"latitude":-32.6,"longitude":149.6,"utc_offset_seconds":39600,
    "current":{"time":"2026-11-14T15:30","interval":900,"temperature_2m":27.4,"weather_code":2,
      "wind_speed_10m":14.2,"wind_gusts_10m":31.0,"is_day":1},
    "daily":{"time":["2026-11-14","2026-11-15","2026-11-16","2026-11-17"],
      "weather_code":[2,61,95,0],"temperature_2m_max":[29.1,24.0,22.3,26.8],
      "temperature_2m_min":[14.2,15.1,12.9,10.4],"precipitation_probability_max":[10,80,95,0],
      "precipitation_sum":[0.0,6.2,18.4,0.0],"wind_gusts_10m_max":[38.2,45.0,72.4,25.9]}})";
  Weather w;
  strcpy(w.place, "Mudgee");
  CHECK(parseOpenMeteo(json, strlen(json), w));
  CHECK(w.valid && w.code == 2 && w.isDay);
  NEAR(w.temp, 27.4f, 0.01f);
  CHECK(w.day[0].wday == 6);  // 14 Nov 2026 is a Saturday
  CHECK(w.day[1].wday == 0);
  CHECK(w.day[2].code == 95 && w.day[2].rainProb == 95);
  NEAR(w.day[2].gustMax, 72.4f, 0.01f);
  CHECK(!strcmp(w.place, "Mudgee"));
  Weather bad;
  CHECK(!parseOpenMeteo("{\"reason\":\"x\"}", 14, bad) && !bad.valid);
  CHECK(wxIcon(0, false) == 0xF02E && wxIcon(63, true) == 0xF019);
  CHECK(!strcmp(wxText(95), "Thunderstorm"));

  JsonDocument d;
  weatherToJson(w, d.to<JsonObject>());
  std::string out;
  serializeJson(d, out);
  CHECK(out.size() < 260);
  Weather w2;
  weatherFromJson(w2, d.as<JsonObjectConst>());
  CHECK(w2.valid && w2.day[3].wday == 2 && w2.day[1].rainProb == 80);
  NEAR(w2.day[2].tMin, 12.9f, 0.01f);
  CHECK(!strcmp(w2.place, "Mudgee"));

  char path[400];
  openMeteoPath(path, sizeof(path), -32.6012f, 149.5867f);
  CHECK(strstr(path, "latitude=-32.601&longitude=149.587") != nullptr);

  // NMEA RMC from a u-blox receiver (checksums computed per spec).
  NmeaParser g;
  auto feed = [&](const char *s) {
    bool any = false;
    for (const char *p = s; *p; p++) any |= g.feed((uint8_t)*p);
    return any;
  };
  auto withSum = [](const char *body) {
    uint8_t x = 0;
    for (const char *p = body; *p; p++) x ^= (uint8_t)*p;
    char b[120];
    snprintf(b, sizeof(b), "$%s*%02X\r\n", body, x);
    return std::string(b);
  };
  CHECK(!feed(withSum("GNRMC,051230.00,V,,,,,,,141126,,,N").c_str()));
  CHECK(!g.hasFix());
  CHECK(feed(withSum("GNRMC,051231.00,A,3236.0720,S,14935.2020,E,0.02,,141126,,,A").c_str()));
  NEAR((float)g.lat(), -32.60120f, 0.00001f);
  NEAR((float)g.lon(), 149.58670f, 0.00001f);
  CHECK(!feed("$GNRMC,051231.00,A,3236.0720,S,14935.2020,E,0.02,,141126,,,A*00\r\n"));  // bad checksum
  CHECK(!feed(withSum("GPGGA,051231.00,3236.0720,S,14935.2020,E,1,08,1.0,450.0,M,,M,,").c_str()));
  NEAR(distanceKm(-33.8688f, 151.2093f, -37.8136f, 144.9631f), 713.4f, 3.0f);  // Sydney-Melbourne
  CHECK(!strcmp(auStateAbbrev("New South Wales"), "NSW"));
  CHECK(!strcmp(auStateAbbrev("Somewhere"), "Somewhere"));
  char gp[160];
  geocodePath(gp, sizeof(gp), "Port Macquarie's");
  CHECK(!strcmp(gp, "/v1/search?count=6&language=en&countryCode=AU&name=Port%20Macquarie%27s"));
}

static void testAcCharge() {
  State s;
  s.bat.online = true;
  s.bat.solarA = 20;   // Victrons
  s.bat.chargerA = 5;  // BMPRO
  AcChargeDetector d;
  uint32_t t = 0;
  auto run = [&](float netA, float loadA, uint32_t ms) {
    s.bat.a = netA;
    for (uint32_t end = t + ms; t < end; t += 100) d.update(s, t, loadA);
  };
  // Solar + BMPRO explain everything: no mains.
  run(15, 10, 120000);
  CHECK(!s.bat.acOn);
  NEAR(s.bat.acA, 0, 0.01);
  NEAR(s.bat.chargeA, 25, 0.01);
  NEAR(s.bat.loadA, 10, 0.01);
  // Chargers plugged in: +100 A into the battery that nothing explains.
  run(115, 10, 10000);
  CHECK(!s.bat.acOn);  // not yet: needs 20 s
  run(115, 10, 15000);
  CHECK(s.bat.acOn);
  run(115, 10, 30000);  // let the smoothing settle
  NEAR(s.bat.acA, 100, 0.5);
  NEAR(s.bat.chargeA, 125, 0.5);
  // A short dip (aircon compressor start) doesn't hide it.
  run(15, 110, 5000);
  CHECK(s.bat.acOn);
  run(115, 10, 10000);
  // Float: chargers drop to 2 A -> hides only after 60 s below 4 A.
  run(17, 10, 30000);
  CHECK(s.bat.acOn);
  run(17, 10, 75000);  // ~30 s to smooth below 4 A, then 60 s hold
  CHECK(!s.bat.acOn);
  NEAR(s.bat.acA, 0, 0.01);
  // No load measurement: loads assumed 0, so AC is a floor (100 - 10 = 90).
  run(115, -1, 60000);
  CHECK(s.bat.acOn);
  NEAR(s.bat.acA, 90, 0.5);
  NEAR(s.bat.loadA, 0, 0.5);
  // Battery offline: everything resets.
  s.bat.online = false;
  d.update(s, t, -1);
  CHECK(!s.bat.acOn);
  // JSON round trip
  s.bat.online = true;
  s.bat.acOn = true;
  s.bat.acA = 87.3f;
  JsonDocument doc;
  stateToJson(s, doc.to<JsonObject>());
  State r;
  stateFromJson(r, doc.as<JsonObjectConst>());
  CHECK(r.bat.acOn);
  NEAR(r.bat.acA, 87.3f, 0.05);
}

static void testHeater() {
  // Command frames: factory passkey 1234 -> 0C 22, checksum = sum of bytes 2..6.
  uint8_t f[airheater::CMD_LEN];
  airheater::encode(airheater::CMD_POWER, 1, 1234, f);
  const uint8_t on[] = {0xAA, 0x55, 0x0C, 0x22, 0x03, 0x01, 0x00, 0x32};
  CHECK(memcmp(f, on, 8) == 0);
  airheater::encode(airheater::CMD_SETPOINT, 22, 1234, f);
  CHECK(f[4] == 4 && f[5] == 22 && f[7] == (uint8_t)(0x0C + 0x22 + 4 + 22));

  // Client command parsing and range checks
  JsonDocument d;
  HeaterCmd c;
  const char *why = nullptr;
  deserializeJson(d, R"({"c":"heater","temp":23})");
  CHECK(heaterCmdFromJson(d.as<JsonObjectConst>(), c, &why) && c.temp == 23 && c.on < 0);
  deserializeJson(d, R"({"c":"heater","temp":40})");
  CHECK(!heaterCmdFromJson(d.as<JsonObjectConst>(), c, &why));
  deserializeJson(d, R"({"c":"heater","level":0})");
  CHECK(!heaterCmdFromJson(d.as<JsonObjectConst>(), c, &why));
  deserializeJson(d, R"({"c":"heater"})");
  CHECK(!heaterCmdFromJson(d.as<JsonObjectConst>(), c, &why));
  deserializeJson(d, R"({"c":"heater","on":true,"mode":"level","level":7})");
  CHECK(heaterCmdFromJson(d.as<JsonObjectConst>(), c, &why) && c.on == 1 && c.mode == HTR_MODE_LEVEL && c.level == 7);

  // Frames for a command: mode first, set point, then on/off.
  Heater cur;
  uint8_t fr[4][airheater::CMD_LEN];
  CHECK(airheater::framesFor(c, cur, 1234, fr, 4) == 3);
  CHECK(fr[0][4] == airheater::CMD_MODE && fr[0][5] == airheater::WIRE_LEVEL);
  CHECK(fr[1][4] == airheater::CMD_SETPOINT && fr[1][5] == 7);
  CHECK(fr[2][4] == airheater::CMD_POWER && fr[2][5] == 1);
  HeaterCmd lvlOnly;
  lvlOnly.level = 4;  // in thermostat mode: switches to level mode first
  CHECK(airheater::framesFor(lvlOnly, cur, 1234, fr, 4) == 2 && fr[0][4] == airheater::CMD_MODE);

  // Status frame: running, thermostat 22 C, 12.8 V, case 142 C, cabin 19 C
  const uint8_t st[] = {0xAA, 0x55, 0x0C, 0x01, 0x00, 0x03, 0x00, 0x00, 0x02, 22, 4,
                        128, 0, 142, 0, 19, 0, 0, 0, 0};
  Heater h;
  CHECK(airheater::decode(st, sizeof(st), h));
  CHECK(h.phase == HTR_RUNNING && h.mode == HTR_MODE_TEMP && h.setC == 22 && h.level == 5);
  NEAR(h.supplyV, 12.8f, 0.01f);
  NEAR(h.bodyC, 142, 0.01f);
  NEAR(h.cabinC, 19, 0.01f);
  uint8_t cool[sizeof(st)];
  memcpy(cool, st, sizeof(st));
  cool[5] = 4;
  cool[15] = 0xFE;  // -2 C
  cool[16] = 0xFF;
  CHECK(airheater::decode(cool, sizeof(cool), h) && h.phase == HTR_COOLING);
  NEAR(h.cabinC, -2, 0.01f);
  const uint8_t enc[] = {0xAA, 0x66, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18};
  CHECK(!airheater::decode(enc, sizeof(enc), h));  // encrypted variant: not decoded yet

  // JSON: absent until a heater is set up; full status only over Bluetooth.
  State s;
  JsonDocument out;
  stateToJson(s, out.to<JsonObject>());
  CHECK(out["htr"].isNull());
  s.htr = h;
  s.htr.link = HTR_LINK_BLE;
  s.htr.online = true;
  s.htr.err = 2;
  out.clear();
  stateToJson(s, out.to<JsonObject>());
  State r;
  stateFromJson(r, out.as<JsonObjectConst>());
  CHECK(r.htr.link == HTR_LINK_BLE && r.htr.online && r.htr.phase == HTR_COOLING && r.htr.err == 2);
  NEAR(r.htr.cabinC, -2, 0.05f);
  s.htr.link = HTR_LINK_RF;
  out.clear();
  stateToJson(s, out.to<JsonObject>());
  CHECK(out["htr"]["cab"].isNull() && out["htr"]["ln"] == 2);

  // Simulator: lights, runs up to the set point, refuses a restart while cooling.
  HeaterSim sim;
  Heater x;
  x.link = HTR_LINK_BLE;
  HeaterCmd go;
  go.on = 1;
  go.temp = 22;
  CHECK(sim.apply(x, go, &why) && x.phase == HTR_STARTING && x.setC == 22);
  CHECK(sim.currentA(x) > 5);  // glow plug
  for (int i = 0; i < 45; i++) sim.tick(x, 1, 12, 12.9f);
  CHECK(x.phase == HTR_RUNNING && x.bodyC > 60);
  for (int i = 0; i < 600; i++) sim.tick(x, 1, 12, 12.9f);
  CHECK(x.cabinC > 20 && x.cabinC < 23.5f);
  HeaterCmd stop;
  stop.on = 0;
  CHECK(sim.apply(x, stop, &why) && x.phase == HTR_COOLING);
  CHECK(!sim.apply(x, go, &why));
  for (int i = 0; i < 31; i++) sim.tick(x, 1, 12, 12.9f);
  CHECK(x.phase == HTR_OFF && sim.currentA(x) == 0);
}


// ---------------------------------------------------------------- secure mode
// Rotate a vector: the sensor can be mounted at any angle.
static Vec3 mount(const Vec3 &v) {
  const float a = 0.5f, b = 1.1f;  // ~29 deg about x, then ~63 deg about z
  Vec3 r{v.x, v.y * std::cos(a) - v.z * std::sin(a), v.y * std::sin(a) + v.z * std::cos(a)};
  return {r.x * std::cos(b) - r.y * std::sin(b), r.x * std::sin(b) + r.y * std::cos(b), r.z};
}

// Arms at t=0, runs `calmSec` of calm (exit delay), then `motion` for
// `sec` seconds. Returns seconds into the motion when it went off, or -1.
static float runGuard(uint8_t sens, uint8_t motion, float sec, bool rotated = false, uint8_t *cause = nullptr,
                      float calmSec = 70, bool instant = false) {
  TheftGuard g;
  MotionSim sim;
  g.setSensitivity(sens);
  uint32_t ms = 1000;
  Vec3 a, w;
  g.sample(rotated ? mount(Vec3{0, 0, 1}) : Vec3{0, 0, 1}, Vec3{}, ms);
  if (instant) g.armNow(ms);
  else g.arm(ms);
  for (float t = 0; t < calmSec; t += 0.01f) {
    ms += 10;
    sim.at(SIM_CALM, t, a, w);
    g.sample(rotated ? mount(a) : a, rotated ? mount(w) : w, ms);
    g.tick(ms, true);
  }
  if (g.mode() != SEC_ARMED) return -2;
  for (float t = 0; t < sec; t += 0.01f) {
    ms += 10;
    sim.at(motion, t, a, w);
    g.sample(rotated ? mount(a) : a, rotated ? mount(w) : w, ms);
    g.tick(ms, true);
    if (g.mode() != SEC_ARMED) {  // movement: the entry delay has started
      if (cause) *cause = g.cause();
      return t;
    }
  }
  return -1;
}

static void testTheftGuard() {
  // Arming: exit delay, then armed.
  {
    TheftGuard g;
    g.sample(Vec3{0, 0, 1}, Vec3{}, 100);
    g.arm(100);
    CHECK(g.mode() == SEC_ARMING);
    Secure s;
    g.fill(s, 100);
    CHECK(s.armLeft == 60);
    uint32_t ms = 100;
    for (int i = 0; i < 5900; i++) {
      ms += 10;
      g.sample(Vec3{0, 0, 1}, Vec3{0.5f, 0, 0}, ms);
      g.tick(ms, true);
    }
    CHECK(g.mode() == SEC_ARMING);
    for (int i = 0; i < 200; i++) {
      ms += 10;
      g.sample(Vec3{0, 0, 1}, Vec3{0.5f, 0, 0}, ms);
      g.tick(ms, true);
    }
    CHECK(g.mode() == SEC_ARMED);
    // Sensor stops: alarm after 5 s.
    for (int i = 0; i < 4; i++) g.tick(ms += 1000, false);
    if (g.mode() != SEC_ARMED) printf("  mode %d cause %d\n", g.mode(), g.cause());
    CHECK(g.mode() == SEC_ARMED);
    for (int i = 0; i < 3; i++) g.tick(ms += 1000, false);
    CHECK(g.mode() == SEC_ENTRY);
    CHECK(g.cause() == SEC_SENSOR);
    // 5 minutes to disarm, then the full alarm.
    g.tick(ms += 1000, false);
    Secure st;
    g.fill(st, ms);
    CHECK(st.armLeft > 290 && st.armLeft <= 300);
    g.tick(ms += 290000, false);
    CHECK(g.mode() == SEC_ENTRY);
    g.tick(ms += 12000, false);
    CHECK(g.mode() == SEC_TRIGGERED);
    g.disarm();
    CHECK(g.mode() == SEC_OFF);
  }
  // Armed from the phone: no exit delay.
  {
    TheftGuard g;
    g.sample(Vec3{0, 0, 1}, Vec3{}, 100);
    g.armNow(100);
    CHECK(g.mode() == SEC_ARMED);
    Secure s;
    g.fill(s, 100);
    CHECK(s.armLeft == 0);
  }
  for (uint8_t sens = SENS_LOW; sens <= SENS_HIGH; sens++) {
    CHECK(runGuard(sens, SIM_WIND, 600, false, nullptr, 0, true) == -1);
    CHECK(runGuard(sens, SIM_WIND, 600, true, nullptr, 0, true) == -1);
    CHECK(runGuard(sens, SIM_CALM, 600, true, nullptr, 0, true) == -1);
  }
  {
    uint8_t c = 0;
    float tj = runGuard(SENS_MEDIUM, SIM_JACK, 120, true, &c, 0, true);
    CHECK(tj > 5 && tj < 35 && c == SEC_TILT);
    float tt = runGuard(SENS_MEDIUM, SIM_TOW, 120, true, &c, 0, true);
    CHECK(tt > 0 && tt < 40);
  }
  // Ten minutes of gusty wind never sets it off, at any sensitivity.
  for (uint8_t sens = SENS_LOW; sens <= SENS_HIGH; sens++) {
    CHECK(runGuard(sens, SIM_WIND, 600) == -1);
    CHECK(runGuard(sens, SIM_WIND, 600, true) == -1);
    CHECK(runGuard(sens, SIM_CALM, 600) == -1);  // gyro bias doesn't add up to a "turn"
  }
  // Winding the jockey wheel up: tilt, within about half a minute.
  uint8_t cause = 0;
  float t = runGuard(SENS_MEDIUM, SIM_JACK, 120, false, &cause);
  CHECK(t > 5 && t < 35);
  CHECK(cause == SEC_TILT);
  t = runGuard(SENS_MEDIUM, SIM_JACK, 120, true, &cause);
  CHECK(t > 5 && t < 35);
  CHECK(runGuard(SENS_LOW, SIM_JACK, 120) > 0);  // 4 deg is past even the low limit
  // Towed away: caught on the turn out of the site.
  cause = 0;
  t = runGuard(SENS_MEDIUM, SIM_TOW, 120, false, &cause);
  CHECK(t > 8 && t < 30);
  CHECK(cause == SEC_TURN || cause == SEC_SHAKE);
  CHECK(runGuard(SENS_MEDIUM, SIM_TOW, 120, true) > 0);
  CHECK(runGuard(SENS_LOW, SIM_TOW, 120) > 0);
  printf("  secure: jack caught after %.0f s, tow after %.0f s\n", runGuard(SENS_MEDIUM, SIM_JACK, 120),
         runGuard(SENS_MEDIUM, SIM_TOW, 120));

  // Resumed after a restart against the saved resting tilt: a van moved
  // while the hub was off is caught.
  {
    TheftGuard g;
    uint32_t ms = 10;
    float p = 3.0f / 57.29578f;
    g.sample(Vec3{std::sin(p), 0, std::cos(p)}, Vec3{}, ms);
    g.resume(Vec3{0, 0, 1}, ms);
    for (int i = 0; i < 2500 && g.mode() != SEC_ENTRY; i++) {
      ms += 10;
      g.sample(Vec3{std::sin(p), 0, std::cos(p)}, Vec3{}, ms);
      g.tick(ms, true);
    }
    CHECK(g.mode() == SEC_ENTRY);
    CHECK(g.cause() == SEC_TILT);
  }

  // Through the alert engine: a Critical alert while triggered, cleared on disarm.
  {
    RuleSet rs;
    loadDefaultRules(rs);
    CHECK(!ensureTheftRule(rs));  // already there
    RuleSet empty;
    CHECK(ensureTheftRule(empty));
    CHECK(empty.count == 1 && empty.rules[0].sev == SEV_WARN && empty.rules[0].levelSev(1) == SEV_CRIT);
    State s;
    AlertEngine e;
    e.evaluate(s, rs, 1000, 0);
    int before = s.alertCount;
    s.sec.mode = SEC_ENTRY;  // movement: a warning, 5 minutes to disarm
    s.sec.cause = SEC_TURN;
    e.evaluate(s, rs, 2000, 0);
    CHECK(s.alertCount == before + 1);
    bool warned = false;
    for (int i = 0; i < s.alertCount; i++)
      warned |= s.alerts[i].sev == SEV_WARN && std::string(s.alerts[i].msg) == "Theft alarm: movement - disarm within 5 min";
    CHECK(warned);
    s.sec.mode = SEC_TRIGGERED;  // not disarmed in time: critical
    e.evaluate(s, rs, 3000, 0);
    CHECK(s.alertCount == before + 1);
    CHECK(s.alerts[0].sev == SEV_CRIT);
    CHECK(std::string(s.alerts[0].msg) == "Theft alarm: van turned (being towed?)");
    s.sec.mode = SEC_OFF;
    s.sec.cause = SEC_NONE;
    e.evaluate(s, rs, 4000, 0);
    CHECK(s.alertCount == before);
  }
  // JSON round trip.
  {
    State s, s2;
    s.sec.mode = SEC_ARMING;
    s.sec.armLeft = 42;
    s.sec.sens = SENS_HIGH;
    s.sec.tilt = 0.4f;
    s.sec.tiltLim = 1.2f;
    s.sec.shake = 3;
    JsonDocument d;
    stateToJson(s, d.to<JsonObject>());
    stateFromJson(s2, d.as<JsonObjectConst>());
    CHECK(s2.sec.mode == SEC_ARMING && s2.sec.armLeft == 42 && s2.sec.sens == SENS_HIGH && s2.sec.shake == 3);
    NEAR(s2.sec.tilt, 0.4f, 0.01f);
  }
}

static void testBleTag() {
  // Victron solar readout: flags + manufacturer data (the advert from testVictronBle).
  const uint8_t vic[] = {0x02, 0x01, 0x06, 0x17, 0xFF, 0xE1, 0x02, 0x10, 0x60, 0xA0, 0xA0, 0x01, 0x2B,
                         0x1A, 0x0D, 0xD2, 0xB7, 0x57, 0x2D, 0xA9, 0xA0, 0x05, 0x98, 0xAD, 0xFE, 0x5D, 0x96};
  BleTag t;
  bleClassify(vic, sizeof(vic), t);
  CHECK(t.kind == BLE_VICTRON && !strcmp(t.label, "victron-solar") && t.mfrId == 0x02E1 && t.mfrLen == 22);

  // iPhone: Apple manufacturer data -> noise.
  const uint8_t apple[] = {0x02, 0x01, 0x1A, 0x07, 0xFF, 0x4C, 0x00, 0x10, 0x02, 0x0B, 0x00};
  bleClassify(apple, sizeof(apple), t);
  CHECK(t.kind == BLE_NOISE);

  // Heater: FFE0 in the service list, name in the advert.
  const uint8_t htr[] = {0x02, 0x01, 0x06, 0x03, 0x03, 0xE0, 0xFF, 0x05, 0x09, 'A', 'I', 'R', '1'};
  bleClassify(htr, sizeof(htr), t);
  CHECK(t.kind == BLE_HEATER && !strcmp(t.name, "AIR1"));

  // BMPRO by name, case-insensitive.
  const uint8_t bm[] = {0x0A, 0x09, 'O', 'd', 'y', 's', 's', 'e', 'y', 'L', 'k'};
  bleClassify(bm, sizeof(bm), t);
  CHECK(t.kind == BLE_BMPRO && !strcmp(t.name, "OdysseyLk"));

  // BTHome thermometer (service data FCD2): 23.45 C.
  const uint8_t bth[] = {0x0A, 0x16, 0xD2, 0xFC, 0x40, 0x02, 0x29, 0x09, 0x01, 0x64, 0x00};
  bleClassify(bth, sizeof(bth), t);
  CHECK(t.kind == BLE_THERMOMETER && t.hasTemp && t.tempC > 23.4f && t.tempC < 23.5f);

  // Unknown and malformed adverts never crash or over-read.
  const uint8_t junk[] = {0x05, 0xFF, 0x34, 0x12, 0x01};
  bleClassify(junk, sizeof(junk), t);
  CHECK(t.kind == BLE_UNKNOWN && !strcmp(t.label, "?"));
  bleClassify(junk, 0, t);
  CHECK(t.kind == BLE_UNKNOWN);
}

static std::vector<uint8_t> unhex(const char *h) {
  std::vector<uint8_t> v;
  for (; h[0] && h[1]; h += 2) {
    unsigned b;
    sscanf(h, "%2x", &b);
    v.push_back((uint8_t)b);
  }
  return v;
}

static void testBmproAdv() {
  // Real adverts from Beau's van, 2026-11-17 (guided survey log).
  auto bat = unhex("02010603030efe15160efe01953505f4ffff6c3101001b182567060000");
  BmproAdvert a;
  CHECK(bmproParseAdvert(bat.data(), bat.size(), a));
  CHECK(a.frame == 1 && a.hasBattery);
  NEAR(a.batV, 13.717f, 0.001f);
  NEAR(a.batA, -3.067f, 0.001f);
  NEAR(a.remainAh, 78.188f, 0.001f);
  CHECK(a.uptimeS == 0x00066725);
  // Big load on: -26.07 A.
  auto load = unhex("02010603030efe15160efe011f352c9affffc33001001b183e68060000");
  CHECK(bmproParseAdvert(load.data(), load.size(), a) && a.hasBattery);
  NEAR(a.batA, -26.068f, 0.001f);
  NEAR(a.batV, 13.599f, 0.001f);
  // Charging: +2.185 A.
  auto chg = unhex("02010603030efe15160efe011f3589080000ed2f01001b18fd68060000");
  CHECK(bmproParseAdvert(chg.data(), chg.size(), a));
  NEAR(a.batA, 2.185f, 0.001f);
  // Charging frame: 9.5 A input (solar), b14 = 1.533 A.
  auto out = unhex("02010603030efe15160efe04000000000000d8c80010000001fd055f00");
  CHECK(bmproParseAdvert(out.data(), out.size(), a) && a.frame == 4 && a.hasInput && !a.hasBattery);
  NEAR(a.inputA, 9.5f, 0.01f);
  NEAR(a.b14A, 1.533f, 0.001f);
  // Identity frame, and the tagger.
  auto id = unhex("02010603030efe15160efe000107004120412440303600292a67060000");
  CHECK(bmproParseAdvert(id.data(), id.size(), a) && a.frame == 0 && a.uptimeS == 0x0006672a);
  BleTag t;
  bleClassify(bat.data(), bat.size(), t);
  CHECK(t.kind == BLE_BMPRO && !strcmp(t.label, "bmpro-battery"));
  bleClassify(out.data(), out.size(), t);
  CHECK(!strcmp(t.label, "bmpro-charging"));
  // A neighbour's different BMPRO model: longer 0x04 frame, not decoded.
  auto other = unhex("02010603030efe17160efe04050400412041252030360067cf279f02004b35");
  CHECK(bmproParseAdvert(other.data(), other.size(), a) && a.frame == 4 && !a.hasInput && !a.hasBattery);
  // Not BMPRO: the heater's advert.
  auto htr = unhex("0201060502e0ffe7fe0bffe02e88a069961c040bf0");
  CHECK(!bmproParseAdvert(htr.data(), htr.size(), a));
  bleClassify(htr.data(), htr.size(), t);
  CHECK(t.kind == BLE_HEATER);
}

static void testBmproLink() {
  // Notifications from the app's HCI log, 2026-11-17 5:25 PM.
  using namespace cv::bmpro;
  Values v;
  Msg m;
  auto f = [&](const char *h) {
    auto b = unhex(h);
    static std::vector<uint8_t> keep;
    keep = b;
    return parse(keep.data(), keep.size(), m) && apply(m, v);
  };
  CHECK(f("1800406104fffff49f"));
  NEAR(v.batA, -2.913f, 0.001f);
  CHECK(f("1800406204000035a9"));
  NEAR(v.batV, 13.737f, 0.001f);
  CHECK(f("180040640400002f7a"));
  NEAR(v.solarA, 12.154f, 0.001f);
  CHECK(f("180040660400000063") && v.hasSoc && v.soc == 99);
  CHECK(f("180040670400001aaf") && v.minLeft == 6831);
  CHECK(f("180090800400020404") && v.hasTanks);
  CHECK(v.tankPct[0] == 100 && v.tankPct[1] == 100 && v.tankPct[2] == 50 && v.tankPct[3] == 0);
  // Pump 1 write reply after "on", pump 2 status "off".
  CHECK(f("1600404c05010000002a") && v.hasPump[0] && v.pumpOn[0]);
  CHECK(f("1800404d050000000000") && v.hasPump[1] && !v.pumpOn[1]);
  // Unknown params are ignored, short frames rejected.
  CHECK(!f("1800406b040000011c"));
  CHECK(!f("18004062"));
  uint8_t buf[8];
  CHECK(encodePump(0, true, buf) == 6);
  CHECK(!memcmp(buf, unhex("3e00404c0101").data(), 6));
  encodePump(1, false, buf);
  CHECK(!memcmp(buf, unhex("3e00404d0100").data(), 6));
}

// Real frames from Beau's Power Science heater (Android HCI snoop of the
// AirHeaterBLE app, 2026-11-18). Room temperature on the app was 26 C.
static void testHeaterReal() {
  uint8_t f[airheater::CMD_LEN];
  airheater::encode(airheater::CMD_STATUS, 0, 1234, f);
  CHECK(std::vector<uint8_t>(f, f + 8) == unhex("aa550c220100002f"));
  airheater::encode(airheater::CMD_POWER, 0, 1234, f);
  CHECK(std::vector<uint8_t>(f, f + 8) == unhex("aa550c2203000031"));
  airheater::encode(airheater::CMD_MODE, airheater::WIRE_TEMP, 1234, f);
  CHECK(std::vector<uint8_t>(f, f + 8) == unhex("aa550c2202020032"));
  airheater::encode(airheater::CMD_SETPOINT, 28, 1234, f);
  CHECK(std::vector<uint8_t>(f, f + 8) == unhex("aa550c22041c004e"));

  Heater h;
  auto idle = unhex("aa5501000000000000000600000000" "1a0020");
  CHECK(airheater::decode(idle.data(), idle.size(), h));
  CHECK(h.phase == HTR_OFF);
  NEAR(h.cabinC, 26, 0.01f);
  auto level = unhex("aa550101000000000107068200150" "01a00c0");
  CHECK(airheater::decode(level.data(), level.size(), h));
  CHECK(h.mode == HTR_MODE_LEVEL && h.level == 7 && h.phase != HTR_OFF);
  NEAR(h.supplyV, 13.0f, 0.01f);
  NEAR(h.bodyC, 21, 0.01f);
  auto therm = unhex("aa550101000064" "00021c01780015001a002b");
  CHECK(airheater::decode(therm.data(), therm.size(), h));
  CHECK(h.mode == HTR_MODE_TEMP && h.setC == 28);
  NEAR(h.supplyV, 12.0f, 0.01f);
  NEAR(h.cabinC, 26, 0.01f);
}

int main() {
  testTheftGuard();
  testHeater();
  testAcCharge();
  testWeather();
  testLeveling();
  testBleSensors();
  testVictronBle();
  testBleTag();
  testBmproAdv();
  testBmproLink();
  testHeaterReal();
  testVeDirect();
  testAlerts();
  testAlertLevels();
  testJsonRoundTrip();
  testSoc();
  testLink();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
