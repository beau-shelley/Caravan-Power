// RS485 client. The hub is bus master: we only transmit straight after a frame
// from the hub arrives, so the half-duplex bus never has two talkers.
#include "LinkCodec.h"
#include "app.h"
#include "board.h"
#include "cloud.h"
#include "evlog.h"
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
Dropouts hubDrops, cloudDrops;

static void trackDrop(Dropouts &d, bool up, const char *what) {
  if (up == d.up) return;
  d.up = up;
  if (!what) {  // tracked, but logged elsewhere
  } else if (up && d.seenUp) evlog::add("%s back after %us", what, (unsigned)((millis() - d.downSinceMs) / 1000));
  else if (up) evlog::add("%s up", what);
  else if (d.seenUp) evlog::add("%s DOWN", what);
  if (up) {
    if (d.seenUp) d.longestS = max<uint32_t>(d.longestS, (millis() - d.downSinceMs) / 1000);
    d.seenUp = true;
  } else if (d.seenUp) {
    d.count++;
    d.downSinceMs = millis();
    time_t now = time(nullptr);
    d.lastAt = now > 1700000000 ? now : 0;
  }
}

static int dropLine(char *b, size_t n, const char *what, const Dropouts &d) {
  if (!d.seenUp) return snprintf(b, n, "%s: not connected yet", what);
  if (!d.count) return snprintf(b, n, "%s: no dropouts", what);
  char at[8] = "?";
  if (d.lastAt) {
    struct tm lt;
    localtime_r(&d.lastAt, &lt);
    snprintf(at, sizeof(at), "%02d:%02d", lt.tm_hour, lt.tm_min);
  }
  uint32_t longest = d.longestS;
  if (!d.up) longest = max<uint32_t>(longest, (millis() - d.downSinceMs) / 1000);
  return snprintf(b, n, "%s: dropped %u x, last %s, longest %um %02us%s", what, (unsigned)d.count, at,
                  (unsigned)(longest / 60), (unsigned)(longest % 60), d.up ? "" : " (DOWN NOW)");
}

void dropoutSummary(char *buf, size_t len) {
  uint32_t m = millis() / 60000;
  int k = snprintf(buf, len, "Since display start %uh %02um.  ", (unsigned)(m / 60), (unsigned)(m % 60));
  if (k < 0 || (size_t)k >= len) return;
  k += dropLine(buf + k, len - k, "Hub link", hubDrops);
  if ((size_t)k + 4 >= len) return;
  k += snprintf(buf + k, len - k, "\n");
  dropLine(buf + k, len - k, "Cloud", cloudDrops);
}

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
  } else if (!strcmp(t, "wx_req")) {  // hub without Wi-Fi wants the forecast
    cloud::requestWeather(doc["lat"] | 0.0f, doc["lon"] | 0.0f);
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
    if (gQCount < QLEN && cloud::takeLocalCommand(c)) send(c);
  }
  // Our turn to talk: the hub just finished a frame and is now listening.
  if (gotFrame && !gSer.available()) transmitOne();

  bool up = gLastRxMs && millis() - gLastRxMs < 4000;
  trackDrop(hubDrops, up, "hub link");
  trackDrop(cloudDrops, cloud::connected(), nullptr);  // cloud.cpp logs its own
  // Pass our NTP clock to the hub once a minute (a hub without Wi-Fi has no other).
  static uint32_t nextTimeMs = 0;
  if (up && !linkUp) {
    nextTimeMs = millis();  // hub (re)started: clock first, then the last forecast
    cloud::resendWeather();
  }
  if (up && (int32_t)(millis() - nextTimeMs) >= 0 && time(nullptr) > 1700000000) {
    nextTimeMs = millis() + 60000;
    JsonDocument t;
    t["c"] = "time";
    t["ts"] = (uint32_t)time(nullptr);
    send(t);
  }
  if (up && !linkUp) {
    gHelloPending = true;  // (re)connected: fetch names + rules
    // Meta from the hub changes rarely; re-ask every reconnect.
  }
  if (up != linkUp) stateDirty = true;
  cloud::onHubLink(up);
  linkUp = up;
}

}  // namespace app
