// RS485 client. The hub is bus master: we only transmit straight after a frame
// from the hub arrives, so the half-duplex bus never has two talkers.
#include "LinkCodec.h"
#include "app.h"
#include "board.h"

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
void cmdPump(int i, bool on) {
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
void cmdRule(const cv::Rule &r) {
  JsonDocument d;
  d["c"] = "rule_set";
  cv::ruleToJson(r, d["rule"].to<JsonObject>());
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
  const char *t = doc["t"] | "";
  if (!strcmp(t, "p")) {
    // poll - nothing to parse
  } else if (!strcmp(t, "state")) {
    cv::stateFromJson(state, doc.as<JsonObjectConst>());
    stateDirty = true;
  } else if (!strcmp(t, "hello")) {
    cv::metaFromJson(meta, doc["meta"]);
    cv::rulesFromJson(rules, doc["rules"]);
    metaVer++;
    rulesVer++;
    stateDirty = true;
  } else if (!strcmp(t, "rules")) {
    cv::rulesFromJson(rules, doc["rules"]);
    rulesVer++;
    stateDirty = true;
  } else if (!strcmp(t, "meta")) {
    cv::metaFromJson(meta, doc.as<JsonObjectConst>());
    metaVer++;
    stateDirty = true;
  } else if (gReplyHandler) {
    gReplyHandler(doc.as<JsonObjectConst>());
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
  // Our turn to talk: the hub just finished a frame and is now listening.
  if (gotFrame && !gSer.available()) transmitOne();

  bool up = gLastRxMs && millis() - gLastRxMs < 4000;
  if (up && !linkUp) {
    gHelloPending = true;  // (re)connected: fetch names + rules
    // Meta from the hub changes rarely; re-ask every reconnect.
  }
  if (up != linkUp) stateDirty = true;
  linkUp = up;
}

}  // namespace app
