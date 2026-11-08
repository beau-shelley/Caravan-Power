// Host-side unit tests for the hardware-independent firmware logic.
// Build & run:  make -C tests   (see tests/Makefile)
#include <cmath>
#include <cstdio>
#include <string>

#include "AlertEngine.h"
#include "CaravanModel.h"
#include "LinkCodec.h"
#include "SocEstimator.h"
#include "VeDirect.h"

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
  CHECK(rs2.rules[6].metric == M_TEMP_FRIDGE && rs2.rules[6].delaySec == 1200);
  CHECK(strcmp(rs2.rules[0].name, rs.rules[0].name) == 0);

  Meta m;
  m.acPresetCount = 2;
  strcpy(m.acPresetName[0], "Cool 22");
  strcpy(m.acPresetName[1], "Heat 20");
  JsonDocument md;
  metaToJson(m, md.to<JsonObject>());
  Meta m2;
  metaFromJson(m2, md.as<JsonObjectConst>());
  CHECK(m2.acPresetCount == 2 && strcmp(m2.acPresetName[1], "Heat 20") == 0);
  CHECK(strcmp(m2.tankName[2], "Grey") == 0);
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

int main() {
  testVeDirect();
  testAlerts();
  testJsonRoundTrip();
  testSoc();
  testLink();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
