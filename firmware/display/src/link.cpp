// RS485 client. The hub is bus master: we only transmit straight after a frame
// from the hub arrives, so the half-duplex bus never has two talkers.
#include "LinkCodec.h"
#include "app.h"
#include "board.h"
#include "cloud.h"
#ifdef DISPLAY_DEMO_MODE
#include "demo.h"
#endif

namespace app {

cv::State state;
cv::Meta meta;
cv::RuleSet rules;
bool linkUp = false;
bool stateDirty = true;
uint32_t metaVer = 0, rulesVer = 0;
uint32_t linkErrors = 0;

static HardwareSerial &gSer = Serial1;
static cv::LinkReader gReader;
static uint32_t gLastRxMs = 0;
static bool gHelloPending = true;
static ReplyHandler gReplyHandler = nullptr;

constexpr int QLEN = 8;
static String gQueue[QLEN];
static int gQHead = 0, gQCount = 0;

void onReply(ReplyHandler h) { gReplyHandler = h; }

void linkBegin() {
  gSer.setRxBufferSize(8192);
  gSer.begin(115200, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
}

void send(JsonDocument &cmd) {
#ifdef DISPLAY_DEMO_MODE
  demo::command(cmd);  // no hub in demo builds: apply the command locally
  return;
#endif
  if (gQCount >= QLEN) return;  // drop if the hub is unreachable
  String s;
  serializeJson(cmd, s);
  gQueue[(gQHead + gQCount) % QLEN] = s;
  gQCount++;
}

void cmd(const char *c) {
  JsonDocument d;
  d["c"] = c;
  send(d);
}
// Pump buttons answer at once: the tile shows the new state straight away and
// holds it for a few seconds while the command goes round the hub, so a state
// frame sent before the hub acted can't flip it back. After that (or if the hub
// says no) the hub's real state wins again.
constexpr uint32_t PUMP_HOLD_MS = 3000;
static uint32_t gPumpHoldUntil[cv::NUM_PUMPS] = {};
static bool gPumpHoldOn[cv::NUM_PUMPS] = {};

static void applyPumpHolds() {
  uint32_t now = millis();
  for (int i = 0; i < cv::NUM_PUMPS; i++) {
    if (!gPumpHoldUntil[i]) continue;
    if ((int32_t)(now - gPumpHoldUntil[i]) >= 0 || state.pump[i].on == gPumpHoldOn[i]) {
      gPumpHoldUntil[i] = 0;  // expired, or the hub has caught up
      continue;
    }
    state.pump[i].on = gPumpHoldOn[i];
  }
}

void cmdPump(int i, bool on) {
  if (i >= 0 && i < cv::NUM_PUMPS) {
    state.pump[i].on = on;
    gPumpHoldOn[i] = on;
    gPumpHoldUntil[i] = (millis() + PUMP_HOLD_MS) | 1;
    stateDirty = true;
  }
  JsonDocument d;
  d["c"] = "pump";
  d["i"] = i;
  d["on"] = on;
  send(d);
}
void cmdInverter(bool on) {
  JsonDocument d;
  d["c"] = "inverter";
  d["on"] = on;
  send(d);
}
void cmdAircon(int preset) {
  JsonDocument d;
  d["c"] = "aircon";
  if (preset < 0) d["off"] = true;
  else d["preset"] = preset;
  send(d);
}

void cmdHeaterOn(bool on) {
  JsonDocument d;
  d["c"] = "heater";
  d["on"] = on;
  send(d);
}
void cmdHeaterSet(const char *key, int value) {
  JsonDocument d;
  d["c"] = "heater";
  d[key] = value;
  send(d);
}
void cmdHeaterMode(bool level) {
  JsonDocument d;
  d["c"] = "heater";
  d["mode"] = level ? "level" : "temp";
  send(d);
}

void cmdSecure(bool arm, const char *code) {
  JsonDocument d;
  d["c"] = "secure";
  d["arm"] = arm;
  if (code) d["code"] = code;
  send(d);
}

static void transmitOne() {
  if (gHelloPending) {
    gHelloPending = false;
    const char *h = "{\"c\":\"hello\"}";
    char tr[5];
    cv::linkTrailer(h, strlen(h), tr);
    gSer.write((const uint8_t *)h, strlen(h));
    gSer.write((const uint8_t *)tr, 4);
    return;
  }
  if (!gQCount) return;
  String &s = gQueue[gQHead];
  char tr[5];
  cv::linkTrailer(s.c_str(), s.length(), tr);
  gSer.write((const uint8_t *)s.c_str(), s.length());
  gSer.write((const uint8_t *)tr, 4);
  s = "";
  gQHead = (gQHead + 1) % QLEN;
  gQCount--;
}

static void handleFrame(const char *line, size_t len) {
  static JsonDocument doc;  // static: state frames are ~1 KB
  if (deserializeJson(doc, line, len)) return;
  // A reply to a command relayed from the cloud: send it back to the app
  // (it may also be a hello / rules reply, handled below as usual).
  if (doc["rid"].is<const char *>()) {
    cloud::onHubReply(line, len);
    if (doc["t"].isNull()) return;  // only the cloud asked for it
  }
  const char *t = doc["t"] | "";
  if (!strcmp(t, "p")) {
    // poll - nothing to parse
  } else if (!strcmp(t, "lvl")) {  // fast level update (10 Hz)
    cv::levelFromJson(state.lvl, doc.as<JsonObjectConst>());
    stateDirty = true;
  } else if (!strcmp(t, "state")) {
    cloud::onHubState(doc);  // also marks net.cloud with our own broker link
    cv::stateFromJson(state, doc.as<JsonObjectConst>());
    applyPumpHolds();
    stateDirty = true;
  } else if (!strcmp(t, "event")) {  // alert raised / cleared, for the cloud
    cloud::onHubEvent(line, len);
  } else if (!strcmp(t, "push")) {  // push alert for ntfy
    cloud::onHubPush(doc.as<JsonObjectConst>());
  } else if (!strcmp(t, "hello")) {
    cloud::onHubMeta(doc["meta"]);
    cloud::onHubRules(doc["rules"]);
    cv::metaFromJson(meta, doc["meta"]);
    cv::rulesFromJson(rules, doc["rules"]);
    metaVer++;
    rulesVer++;
    stateDirty = true;
  } else if (!strcmp(t, "rules")) {
    cloud::onHubRules(doc["rules"]);
    cv::rulesFromJson(rules, doc["rules"]);
    rulesVer++;
    stateDirty = true;
  } else if (!strcmp(t, "meta")) {
    cloud::onHubMeta(doc);
    cv::metaFromJson(meta, doc.as<JsonObjectConst>());
    metaVer++;
    stateDirty = true;
  } else {
    if (!strcmp(t, "err")) {  // the hub refused a command: drop any pump the tile was holding
      for (auto &h : gPumpHoldUntil) h = 0;
    }
    if (gReplyHandler) gReplyHandler(doc.as<JsonObjectConst>());
  }
}

void linkLoop() {
  bool gotFrame = false;
  while (gSer.available()) {
    if (gReader.feed((uint8_t)gSer.read())) {
      gLastRxMs = millis();
      gotFrame = true;
      handleFrame(gReader.line(), gReader.length());
    }
  }
  linkErrors = gReader.errors();
  // Commands from the app / web page through the cloud broker.
  {
    JsonDocument c;
    while (gQCount < QLEN && cloud::takeCommand(c)) {
      send(c);
      c.clear();
    }
  }
  // Our turn to talk: the hub just finished a frame and is now listening.
  if (gotFrame && !gSer.available()) transmitOne();

  bool up = gLastRxMs && millis() - gLastRxMs < 4000;
  if (up && !linkUp) {
    gHelloPending = true;  // (re)connected: fetch names + rules
    // Meta from the hub changes rarely; re-ask every reconnect.
  }
  if (up != linkUp) stateDirty = true;
  cloud::onHubLink(up);
  linkUp = up;
}

}  // namespace app
