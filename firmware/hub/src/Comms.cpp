#include "Comms.h"

#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <lwip/sockets.h>
#include <ping/ping_sock.h>
#include <lwip/ip_addr.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_random.h>
#include <esp_heap_caps.h>

#include "HubConfig.h"
#include "LinkCodec.h"
#include "root_ca.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "src/secrets.h missing - copy secrets.example.h and fill it in"
#include "secrets.example.h"
#endif

// The classic ESP32 hub hasn't the RAM for TLS next to Wi-Fi + Bluetooth, so by
// default the display board (ESP32-S3, 8 MB PSRAM) does the cloud MQTT link and
// the ntfy push alerts. The hub hands it state, alert events and pushes over
// RS485 and runs cloud commands it relays. Set CLOUD_ON_DISPLAY 0 in secrets.h
// for a hub with no display.
#ifndef CLOUD_ON_DISPLAY
#define CLOUD_ON_DISPLAY 1
#endif

// The van-visit (Bluetooth survey) build runs on the van's Starlink Wi-Fi, so
// it must not leave the dashboard / app open without a login.
#ifdef HUB_BLE_SURVEY
static_assert(sizeof(WEB_USER) > 1 && sizeof(WEB_PASS) > 1,
              "hub-esp32-survey: set WEB_USER and WEB_PASS in src/secrets.h (the bench build may leave them empty)");
#endif

namespace hub {

DisplayLink gLink;
LocalWeb gWeb;
Cloud gCloud;
Notifier gNotify;
Net gNet;
SemaphoreHandle_t gStateMux = nullptr;  // created in setup()

static uint32_t gCloudConnects = 0;
static struct {
  uint32_t boots = 0, prevUp = 0, prevHmin = 0, prevHlmin = 0, prevCc = 0;
  uint8_t prevRr = 0;
  char prevWhy[40] = "";  // why the hub restarted itself last time ("" = it didn't)
} gHealth;
static uint32_t gLargestLow = 0xFFFFFFFF;  // lowest largest-free-block seen (sampled every 30 s)
bool gTlsInPsram = false;

// Internal RAM a TLS session needs in one block: ~40 KB while mbedTLS uses
// internal RAM; with its buffers in PSRAM only the socket and handshake
// scratch do.
static uint32_t tlsBlockNeed() { return gTlsInPsram ? 16000 : 40000; }

// ---------------------------------------------------------------- PSRAM JSON
void *PsramJsonAllocator::allocate(size_t n) {
#ifdef BOARD_HAS_PSRAM
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p) return p;
#endif
  return malloc(n);
}
void PsramJsonAllocator::deallocate(void *p) { free(p); }  // free() handles both kinds of RAM
void *PsramJsonAllocator::reallocate(void *p, size_t n) {
#ifdef BOARD_HAS_PSRAM
  void *q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (q || !n) return q;
#endif
  return realloc(p, n);
}
PsramJsonAllocator *PsramJsonAllocator::instance() {
  static PsramJsonAllocator a;
  return &a;
}

// ---------------------------------------------------------------- restarts
void restartFor(const char *why) {
  Serial.printf("[sys] restarting: %s (heap %u, largest %u)\n", why, (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
  Preferences p;
  if (p.begin("health", false)) {
    p.putString("why", why);
    p.end();
  }
  delay(200);  // let the log line out
  ESP.restart();
  for (;;) {
  }
}

static volatile uint32_t gBeatMs[BEAT_COUNT] = {};
void beat(BeatId id) { gBeatMs[id] = millis() | 1; }

void beatCheck(uint32_t) {
  static const char *kWhy[BEAT_COUNT] = {"BMPRO Bluetooth task stuck", "heater Bluetooth task stuck",
                                         "push alert task stuck"};
  static const int32_t kLimitMs[BEAT_COUNT] = {180000, 180000, 300000};
  for (int i = 0; i < BEAT_COUNT; i++) {
    // Read the beat BEFORE the clock. The tasks beat from other cores/tasks,
    // so a beat can land after loop() took its "now": with now < beat,
    // now - beat wrapped to ~4 billion ms and every boot restarted itself
    // within seconds (2026-11-27 bench, PR 40). Signed difference as well.
    uint32_t b = gBeatMs[i];
    if (!b) continue;
    int32_t age = (int32_t)(millis() - b);
    if (age > kLimitMs[i]) restartFor(kWhy[i]);
  }
}

// ================================================================ Net
// Up to three Wi-Fi networks (e.g. home, phone hotspot, Starlink), the same
// list as the display. Define WIFI_SSID2 / WIFI_PASS2 and WIFI_SSID3 /
// WIFI_PASS3 in secrets.h; the hub moves to the next one every 30 s until one
// connects.
#if defined(WIFI_SSID2) && defined(WIFI_SSID3)
static const char *const kSsid[] = {WIFI_SSID, WIFI_SSID2, WIFI_SSID3};
static const char *const kPass[] = {WIFI_PASS, WIFI_PASS2, WIFI_PASS3};
#elif defined(WIFI_SSID2)
static const char *const kSsid[] = {WIFI_SSID, WIFI_SSID2};
static const char *const kPass[] = {WIFI_PASS, WIFI_PASS2};
#else
static const char *const kSsid[] = {WIFI_SSID};
static const char *const kPass[] = {WIFI_PASS};
#endif
static int gNetIdx = 0;
static void joinNet(int idx) {
  gNetIdx = idx % (int)(sizeof(kSsid) / sizeof(kSsid[0]));
  WiFi.begin(kSsid[gNetIdx], kPass[gNetIdx]);
  Serial.printf("[net] connecting to '%s'\n", kSsid[gNetIdx]);
}

void Net::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  joinNet(0);
  // By IP address (Google's anycast time servers), not host name: on Arduino 3 /
  // IDF 5 an SNTP host-name lookup that fires while the MQTT connect resolves
  // the broker creates a UDP socket without the lwIP core lock and the hub
  // panics (2026-11-26 coredump: udp_new_ip_type assert from sntp_dns_found,
  // every ~40 min). The display hit the same bug (d2d2f6d).
  configTzTime(TZ_INFO, "216.239.35.0", "216.239.35.4");
}

// Wi-Fi off build: the display sends the time ({"c":"time"}); keep the
// local timezone rules so midnight resets and the UTC offset still work.
void Net::beginOffline() {
  setenv("TZ", TZ_INFO, 1);
  tzset();
  WiFi.mode(WIFI_OFF);
  offline_ = true;
  Serial.println("[net] Wi-Fi off in this build (the display board runs the cloud and the clock)");
}

bool Net::timeValid() const { return time(nullptr) > 1700000000; }

// ---- watchdog
static volatile int gPingReplies = 0;
static void onPingOk(esp_ping_handle_t, void *) { gPingReplies++; }

void Net::watchdog() {
  uint32_t now = millis();
  if (ping_) {  // a ping round started last time: done yet?
    if ((int32_t)(now - pingDoneMs_) < 0) return;
    esp_ping_stop((esp_ping_handle_t)ping_);
    esp_ping_delete_session((esp_ping_handle_t)ping_);
    ping_ = nullptr;
    if (gPingReplies > 0 || !lastPingOkMs_) {
      lastPingOkMs_ = now;
      return;
    }
    uint32_t dead = now - lastPingOkMs_;
    Serial.printf("[net] router not answering for %lu s (heap %u, min %u)\n", (unsigned long)(dead / 1000),
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
    if (dead < 90000) return;
    if (!rejoinedMs_ || now - rejoinedMs_ > 600000) {
      Serial.println("[net] network jammed - rejoining Wi-Fi");
      rejoinedMs_ = now;
      lastPingOkMs_ = now;
      WiFi.disconnect(false);
      delay(200);
      joinNet(gNetIdx);
    } else {
      restartFor("network jammed after rejoining Wi-Fi");
    }
    return;
  }
  if ((int32_t)(now - nextPingMs_) < 0) return;
  nextPingMs_ = now + 30000;
  if (WiFi.status() != WL_CONNECTED) {
    lastPingOkMs_ = now;  // Net::loop() handles plain disconnects
    return;
  }
  IPAddress gw = WiFi.gatewayIP();
  if (gw == IPAddress(0, 0, 0, 0)) return;
  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr.type = IPADDR_TYPE_V4;
  cfg.target_addr.u_addr.ip4.addr = (uint32_t)gw;
  cfg.count = 3;
  cfg.interval_ms = 500;
  cfg.timeout_ms = 1000;
  cfg.task_stack_size = 3072;
  esp_ping_callbacks_t cbs = {};
  cbs.on_ping_success = onPingOk;
  esp_ping_handle_t h = nullptr;
  gPingReplies = 0;
  if (esp_ping_new_session(&cfg, &cbs, &h) != ESP_OK) return;
  esp_ping_start(h);
  ping_ = h;
  pingDoneMs_ = now + 4000;
}

void Net::loop(cv::State &s) {
  // The web server, WebSockets and MQTT all run on the net task: if it hasn't
  // looped for 2 minutes it's stuck (e.g. a socket write that never returns).
  uint32_t alive = netAliveMs_;  // read before the clock: the net task updates it (see beatCheck)
  if (alive && (int32_t)(millis() - alive) > 120000) {
    restartFor("net task stuck for 2 min");
  }
  static uint32_t nextHeapLog = 0;
  if ((int32_t)(millis() - nextHeapLog) >= 0) {
    nextHeapLog = millis() + 30000;
    if (ESP.getMaxAllocHeap() < gLargestLow) gLargestLow = ESP.getMaxAllocHeap();
    Serial.printf("[sys] heap %u free, min %u, largest %u; PSRAM %u free of %u\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getFreePsram(),
                  (unsigned)ESP.getPsramSize());
    // Unused stack per task (bytes never touched): what each could give back.
    static const char *kTasks[] = {"net", "push", "heater", "bmpro", "nimble_host", "loopTask", "ble_scan"};
    char line[160];
    int n = snprintf(line, sizeof(line), "[sys] stack unused:");
    for (const char *t : kTasks) {
      TaskHandle_t h = xTaskGetHandle(t);
      if (h && n < (int)sizeof(line)) n += snprintf(line + n, sizeof(line) - n, " %s %u", t, (unsigned)uxTaskGetStackHighWaterMark(h));
    }
    Serial.println(line);
  }
  static bool was = false;
  static uint32_t lastTry = 0;
  bool up = !offline_ && WiFi.status() == WL_CONNECTED;
  if (up && !was) {
    Serial.printf("[net] Wi-Fi up, IP %s (heap %u free, largest %u)\n", WiFi.localIP().toString().c_str(),
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    MDNS.begin(HOSTNAME);
    MDNS.addService("http", "tcp", 80);
  }
  if (!up && !offline_ && millis() - lastTry > 30000) {  // belt and braces
    lastTry = millis();
    if (sizeof(kSsid) / sizeof(kSsid[0]) > 1) {
      WiFi.disconnect(false);
      joinNet(gNetIdx + 1);  // try the other network
    } else {
      WiFi.reconnect();
    }
  }
  was = up;
  s.wifi = up;
  strncpy(s.ip, up ? WiFi.localIP().toString().c_str() : "", sizeof(s.ip) - 1);
  s.ts = timeValid() ? (uint32_t)time(nullptr) : 0;
  if (s.ts) {
    // UTC offset incl. DST, so the display/web can show local time without
    // knowing the timezone rules themselves.
    time_t now = s.ts;
    struct tm lt;
    localtime_r(&now, &lt);
    struct tm ut;
    gmtime_r(&now, &ut);
    ut.tm_isdst = lt.tm_isdst;
    s.tzOff = (int32_t)(now - mktime(&ut));
  }
}

// ================================================================ RS485 link
static constexpr uint32_t kPollMs = 100;
// Half duplex: the hub is bus master. It sends a state frame every second and
// a tiny poll frame every 100 ms (so a button press on the display is heard quickly); the display only transmits right after it
// receives a frame, so the two never talk over each other.
// DE (driver enable, active high) and /RE (receiver enable, active low) can
// be bridged on one pin or wired to two; either way HIGH = transmit, LOW = listen.
void DisplayLink::txEnable(bool on) {
  if (de_ >= 0) digitalWrite(de_, on ? HIGH : LOW);
  if (re_ >= 0) digitalWrite(re_, on ? HIGH : LOW);
}

void DisplayLink::begin(HardwareSerial &ser, int rx, int tx, int de, int re) {
  ser_ = &ser;
  de_ = de;
  re_ = re;
  if (de_ >= 0) pinMode(de_, OUTPUT);
  if (re_ >= 0) pinMode(re_, OUTPUT);
  txEnable(false);
  ser_->setRxBufferSize(4096);
  ser_->setTxBufferSize(2048);
  outQ_ = xQueueCreate(8, sizeof(LinkOut));
  ser_->begin(115200, SERIAL_8N1, rx, tx);
}

void DisplayLink::write(const char *buf, size_t n) {
  char tr[5];
  cv::linkTrailer(buf, n, tr);
  txEnable(true);
  ser_->write((const uint8_t *)buf, n);
  ser_->write((const uint8_t *)tr, 4);
  ser_->flush();  // wait until the last bit is out before releasing the bus
  txEnable(false);
}

void DisplayLink::send(JsonDocument &doc) {
  static String out;
  out = "";
  serializeJson(doc, out);
  write(out.c_str(), out.length());
}

void DisplayLink::loop(const cv::State &s) {
  static cv::LinkReader reader;
  while (ser_->available()) {
    if (!reader.feed((uint8_t)ser_->read())) continue;
    lastRxMs_ = millis();
    PsDoc in;
    if (deserializeJson(in, reader.line(), reader.length())) continue;
    if (in["c"].is<const char *>()) {
      // "via":"cloud" = the display relaying a command from the app / web page
      // through the cloud broker: same rules as the hub's own MQTT link.
      bool cloud = !strcmp(in["via"] | "", "cloud");
      PsDoc reply;
      handleCommand(in.as<JsonObjectConst>(), cloud ? SRC_CLOUD : SRC_DISPLAY, reply);
      if (cloud && reply.isNull()) reply["ok"] = true;  // the app waits for an answer
      if (cloud && in["rid"].is<const char *>()) reply["rid"] = in["rid"];
      if (!reply.isNull()) send(reply);
    }
  }
  uint32_t now = millis();
  LinkOut out;
  if (pending_.length()) {  // e.g. town search results from the network task
    write(pending_.c_str(), pending_.length());
    pending_ = "";
    nextPollMs_ = now + kPollMs;
  } else if (outQ_ && xQueueReceive(outQ_, &out, 0) == pdTRUE) {  // alert events / pushes for the cloud
    write(out.json, strlen(out.json));
    nextPollMs_ = now + kPollMs;
  } else if (configDirty_) {
    configDirty_ = false;
    PsDoc doc;
    PsDoc cmd;
    cmd["c"] = "hello";
    handleCommand(cmd.as<JsonObjectConst>(), SRC_DISPLAY, doc);
    send(doc);
    nextPollMs_ = now + kPollMs;
  } else if ((int32_t)(now - nextStateMs_) >= 0) {
    nextStateMs_ = now + 1000;
    nextPollMs_ = now + kPollMs;
    PsDoc doc;
    cv::stateToJson(s, doc.to<JsonObject>());
    send(doc);
  } else if ((int32_t)(now - nextPollMs_) >= 0) {
    nextPollMs_ = now + kPollMs;
    if (s.lvl.online) {
      // The poll slot carries the level reading, so the touchscreen's level
      // page updates 10x a second instead of once.
      PsDoc doc;
      JsonObject o = doc.to<JsonObject>();
      o["t"] = "lvl";
      cv::levelToJson(s.lvl, o);
      send(doc);
    } else {
      write("{\"t\":\"p\"}", 9);
    }
  }
}

// ================================================================ local web
static WebServer gHttp(80);
// A socket write to a phone that isn't reading (asleep, walked off, Wi-Fi busy
// sharing the radio with Bluetooth) blocks inside WiFiClient::write() for
// seconds at a time; with 1 s pushes to several clients the net task never
// caught up and everything jammed. So the periodic push only goes to clients
// whose socket can take data right now; one that can't for 10 s is dropped
// (the phone reconnects by itself).
class HubWs : public WebSocketsServer {
 public:
  using WebSocketsServer::WebSocketsServer;
  bool writable(uint8_t n) {
    if (n >= WEBSOCKETS_SERVER_CLIENT_MAX || !_clients[n].tcp) return false;
    int fd = _clients[n].tcp->fd();
    if (fd < 0) return false;
    fd_set w;
    FD_ZERO(&w);
    FD_SET(fd, &w);
    timeval tv = {0, 0};
    return select(fd + 1, nullptr, &w, nullptr, &tv) > 0;
  }
};
static HubWs gWs(81);
static uint32_t gWsStuckSinceMs[WEBSOCKETS_SERVER_CLIENT_MAX] = {};
static char gWsToken[33] = {0};
static bool gWsAuthed[WEBSOCKETS_SERVER_CLIENT_MAX] = {};

static bool checkAuth() {
  if (strlen(WEB_USER) == 0) return true;
  if (gHttp.authenticate(WEB_USER, WEB_PASS)) return true;
  gHttp.requestAuthentication(BASIC_AUTH, "Caravan");
  return false;
}

static void sendJson(JsonDocument &doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  gHttp.send(code, "application/json", out);
}

static void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len) {
  if (type == WStype_CONNECTED) {
    gWsAuthed[num] = strlen(WEB_USER) == 0;
  } else if (type == WStype_DISCONNECTED) {
    gWsAuthed[num] = false;
  } else if (type == WStype_TEXT) {
    PsDoc in;
    if (deserializeJson(in, payload, len)) return;
    if (!gWsAuthed[num]) {
      // Browsers can't send Basic auth on a WebSocket, so the page first
      // fetches a token from /api/token (which IS behind Basic auth).
      if (strcmp(in["c"] | "", "auth") == 0 && strcmp(in["tok"] | "", gWsToken) == 0) {
        gWsAuthed[num] = true;
        gWs.sendTXT(num, "{\"t\":\"auth\",\"ok\":true}");
      } else {
        gWs.sendTXT(num, "{\"t\":\"auth\",\"ok\":false}");
      }
      return;
    }
    if (strcmp(in["c"] | "", "auth") == 0) {  // already in (no web login set)
      gWs.sendTXT(num, "{\"t\":\"auth\",\"ok\":true}");
      return;
    }
    PsDoc reply;
    handleCommand(in.as<JsonObjectConst>(), SRC_LOCAL_WEB, reply);
    if (!reply.isNull()) {
      String out;
      serializeJson(reply, out);
      gWs.sendTXT(num, out);
    }
  }
}

void LocalWeb::begin() {
  for (int i = 0; i < 16; i++) sprintf(gWsToken + i * 2, "%02x", (uint8_t)esp_random());

  gHttp.on("/", HTTP_GET, [] {
    if (!checkAuth()) return;
    // Gzipped copy from `pio run -t uploadfs` (copy_web.py); streamFile adds
    // Content-Encoding: gzip for a .gz name. Older uploads have the plain file.
    File f = LittleFS.open("/index.html.gz", "r");
    if (!f) f = LittleFS.open("/index.html", "r");
    if (!f) {
      gHttp.send(500, "text/plain", "index.html missing - run: pio run -t uploadfs");
      return;
    }
    gHttp.sendHeader("Cache-Control", "no-cache");
    gHttp.streamFile(f, "text/html");
    f.close();
  });
  gHttp.on("/api/token", HTTP_GET, [] {
    if (!checkAuth()) return;
    PsDoc d;
    d["tok"] = gWsToken;
    d["wsPort"] = 81;
    sendJson(d);
  });
  gHttp.on("/api/state", HTTP_GET, [] {
    if (!checkAuth()) return;
    PsDoc reply, cmd;
    cmd["c"] = "state";
    handleCommand(cmd.as<JsonObjectConst>(), SRC_LOCAL_WEB, reply);
    sendJson(reply);
  });
  gHttp.on("/api/cmd", HTTP_POST, [] {
    if (!checkAuth()) return;
    PsDoc in, reply;
    if (deserializeJson(in, gHttp.arg("plain"))) {
      gHttp.send(400, "text/plain", "bad json");
      return;
    }
    handleCommand(in.as<JsonObjectConst>(), SRC_LOCAL_WEB, reply);
    if (reply.isNull()) reply["ok"] = true;
    sendJson(reply);
  });
  gHttp.on("/api/config", HTTP_GET, [] {
    if (!checkAuth()) return;
    PsDoc d;
    {
      StateLock l;
      gConfig.toJson(d.to<JsonObject>());
    }
    sendJson(d);
  });
  gHttp.on("/api/config", HTTP_POST, [] {
    if (!checkAuth()) return;
    PsDoc in;
    if (deserializeJson(in, gHttp.arg("plain"))) {
      gHttp.send(400, "text/plain", "bad json");
      return;
    }
    PsDoc cmd, reply;
    cmd["c"] = "config_set";
    cmd["cfg"] = in.as<JsonObjectConst>();
    handleCommand(cmd.as<JsonObjectConst>(), SRC_LOCAL_WEB, reply);
    sendJson(reply);
  });
  gHttp.on("/api/bmpro/log", HTTP_GET, [] {
    if (!checkAuth()) return;
    PsDoc cmd, reply;
    cmd["c"] = "bmpro_log";
    handleCommand(cmd.as<JsonObjectConst>(), SRC_LOCAL_WEB, reply);
    gHttp.send(200, "text/plain", reply["log"] | "");
  });
  gHttp.onNotFound([] { gHttp.send(404, "text/plain", "not found"); });
  gHttp.begin();
  gWs.begin();
  gWs.onEvent(onWsEvent);
}

void LocalWeb::broadcast(JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
    if (!gWsAuthed[i] || !gWs.clientIsConnected(i)) {
      gWsStuckSinceMs[i] = 0;
      continue;
    }
    if (!gWs.writable(i)) {  // not draining: skip this round
      if (!gWsStuckSinceMs[i]) gWsStuckSinceMs[i] = millis() | 1;
      else if (millis() - gWsStuckSinceMs[i] > 10000) {
        Serial.printf("[web] client %u not reading for 10 s - dropping it\n", i);
        gWs.disconnect(i);
        gWsStuckSinceMs[i] = 0;
      }
      continue;
    }
    gWsStuckSinceMs[i] = 0;
    gWs.sendTXT(i, out);
  }
}

void LocalWeb::loop(const cv::State &s) {
  gHttp.handleClient();
  gWs.loop();
  if ((int32_t)(millis() - nextPushMs_) >= 0 && gWs.connectedClients() > 0) {
    nextPushMs_ = millis() + 1000;
    PsDoc doc;
    {
      StateLock l;
      cv::stateToJson(s, doc.to<JsonObject>());
    }
    broadcast(doc);
  }
}

// ================================================================ cloud MQTT
// Topics (all under caravan/<DEVICE_ID>/):
//   state   retained  full state JSON every cloudIntervalSec (and on change)
//   meta    retained  names, presets, capacity
//   rules   retained  alert rules
//   event             alert raised/cleared
//   online  retained  "1" / "0" (last will)
//   cmd     <- subscribed: commands from the remote dashboard
//   resp              replies to commands
static WiFiClientSecure gTls;
static PubSubClient gMqtt(gTls);
static String gBase = String("caravan/") + DEVICE_ID + "/";

// Alert events are queued from the logic task and published from the cloud loop.
static QueueHandle_t gEventQ = nullptr;
struct EventMsg {
  char json[200];
};

static void onMqtt(char *topic, byte *payload, unsigned int len) {
  PsDoc in, reply;
  if (deserializeJson(in, payload, len)) return;
  handleCommand(in.as<JsonObjectConst>(), SRC_CLOUD, reply);
  if (!reply.isNull()) {
    if (in["rid"].is<const char *>()) reply["rid"] = in["rid"];  // correlate replies
    gCloud.publish("resp", reply);
  }
}

void DisplayLink::queueOut(JsonDocument &doc) {
  if (!outQ_) return;
  LinkOut m;
  serializeJson(doc, m.json, sizeof(m.json));
  xQueueSend(outQ_, &m, 0);
}

static uint8_t gMemSkips = 0;  // cloud reconnects skipped for lack of RAM, in a row

void addSysStats(JsonObject o) {
  JsonObject s = o["sys"].to<JsonObject>();
  s["up"] = millis() / 1000;
  s["rr"] = (int)esp_reset_reason();
  s["hf"] = ESP.getFreeHeap();
  s["hmin"] = ESP.getMinFreeHeap();
  s["hl"] = ESP.getMaxAllocHeap();
  s["hlmin"] = gLargestLow == 0xFFFFFFFF ? 0 : gLargestLow;
  s["cc"] = gCloudConnects;
  s["pf"] = ESP.getFreePsram();   // PSRAM free / total: shows whether it's in use
  s["ps"] = ESP.getPsramSize();
  // Saved across restarts and power cuts (NVS): how many boots, and the
  // previous run's figures, so a soak test survives an unplug or a restart.
  s["boots"] = gHealth.boots;
  JsonObject p = s["prev"].to<JsonObject>();
  p["up"] = gHealth.prevUp;
  p["rr"] = gHealth.prevRr;
  p["hmin"] = gHealth.prevHmin;
  p["hlmin"] = gHealth.prevHlmin;
  p["cc"] = gHealth.prevCc;
  if (gHealth.prevWhy[0]) p["why"] = gHealth.prevWhy;
}

// Called at boot: load last run's figures and count this boot.
void healthBegin() {
  Preferences p;
  if (!p.begin("health", false)) return;
  gHealth.boots = p.getUInt("boots", 0) + 1;
  p.putUInt("boots", gHealth.boots);
  gHealth.prevUp = p.getUInt("up", 0);
  gHealth.prevRr = p.getUChar("rrNow", 0);  // the reason the run before this one started
  gHealth.prevHmin = p.getUInt("hmin", 0);
  gHealth.prevHlmin = p.getUInt("hlmin", 0);
  gHealth.prevCc = p.getUInt("cc", 0);
  // Saved by restartFor(); cleared so a power cut or crash next time isn't
  // blamed on an old reason (the reset reason "rr" tells those apart).
  if (p.isKey("why")) {
    p.getString("why", gHealth.prevWhy, sizeof(gHealth.prevWhy));
    p.remove("why");
  }
  p.putUChar("rrNow", (uint8_t)esp_reset_reason());
  p.end();
}

// Every 10 minutes: save this run's figures (a few bytes; NVS wear is negligible).
void healthLoop() {
  static uint32_t next = 600000;
  if ((int32_t)(millis() - next) < 0) return;
  next = millis() + 600000;
  Preferences p;
  if (!p.begin("health", false)) return;
  p.putUInt("up", millis() / 1000);
  p.putUInt("hmin", ESP.getMinFreeHeap());
  p.putUInt("hlmin", gLargestLow == 0xFFFFFFFF ? 0 : gLargestLow);
  p.putUInt("cc", gCloudConnects);
  p.end();
}

static bool cloudConfigured() {
  return !CLOUD_ON_DISPLAY && strlen(MQTT_HOST) && !strstr(MQTT_HOST, "xxxxxxxx");
}

void Cloud::begin() {
  if (CLOUD_ON_DISPLAY) {
    Serial.println("[cloud] the display board runs the cloud link and push alerts");
    return;
  }
  if (!cloudConfigured()) {
    Serial.println("[cloud] MQTT not configured - skipping");
    return;
  }
  gTls.setCACert(ROOT_CA_PEM);
  gMqtt.setServer(MQTT_HOST, MQTT_PORT);
  gMqtt.setBufferSize(6144);
  gMqtt.setKeepAlive(60);  // Starlink handovers can stall a few seconds
  gMqtt.setSocketTimeout(10);
  gMqtt.setCallback(onMqtt);
  gEventQ = xQueueCreate(8, sizeof(EventMsg));
}

bool Cloud::connected() { return gMqtt.connected(); }

void Cloud::publish(const char *sub, JsonDocument &doc, bool retain) {
  if (!gMqtt.connected()) return;
  String out;
  serializeJson(doc, out);
  gMqtt.publish((gBase + sub).c_str(), (const uint8_t *)out.c_str(), out.length(), retain);
}

void Cloud::queueEvent(JsonDocument &doc) {
  if (CLOUD_ON_DISPLAY) return gLink.queueOut(doc);  // {"t":"event",...}
  if (!gEventQ) return;
  EventMsg m;
  serializeJson(doc, m.json, sizeof(m.json));
  xQueueSend(gEventQ, &m, 0);
}

void Cloud::loop(const cv::State &s) {
  if (!cloudConfigured()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (pauseReq_) {  // a push alert needs the RAM
    if (gMqtt.connected()) {
      Serial.println("[cloud] pausing so a push alert can go out");
      gMqtt.disconnect();
      gTls.stop();
    }
    if ((int32_t)(millis() - pauseUntilMs_) < 0) return;
    pauseReq_ = false;
    nextTryMs_ = 0;
  }
  if (!gMqtt.connected()) {
    if ((int32_t)(millis() - nextTryMs_) < 0) return;
    // A TLS handshake needs a ~40 KB block. With both Bluetooth links open the
    // classic ESP32 hub rarely has one, and failed handshakes fragment the
    // heap further (2026-11-19: largest block shrank ~1 KB a minute). Wait
    // for memory instead of trying; the display board will take over cloud.
    if (ESP.getMaxAllocHeap() < tlsBlockNeed()) {
      // Fragmented RAM doesn't heal by itself (2026-11-25: after a push alert
      // the S3 hub sat at a 33 KB largest block and the cloud stayed down for
      // hours). Phone alerts need the cloud, so retry every minute and after
      // three tries (~3 min) restart: Bluetooth, the display link and the
      // cloud are all back within ~30 s with a clean heap.
      if (++gMemSkips >= 3) restartFor("no RAM block for the cloud link");
      Serial.printf("[cloud] skipped: largest free block %u bytes, need %u - retry in 1 min\n",
                    (unsigned)ESP.getMaxAllocHeap(), (unsigned)tlsBlockNeed());
      nextTryMs_ = millis() + 60000;
      return;
    }
    String will = gBase + "online";
    String cid = String(DEVICE_ID) + "-hub-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.printf("[cloud] connecting to %s:%d\n", MQTT_HOST, MQTT_PORT);
    if (gMqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS, will.c_str(), 1, true, "0")) {
      Serial.println("[cloud] connected");
      gMemSkips = 0;
      gCloudConnects++;
      backoffMs_ = 2000;
      gMqtt.publish(will.c_str(), "1", true);
      gMqtt.subscribe((gBase + "cmd").c_str(), 1);
      metaDirty_ = true;
      nextStateMs_ = 0;
    } else {
      Serial.printf("[cloud] failed rc=%d, retry in %us\n", gMqtt.state(), backoffMs_ / 1000);
      nextTryMs_ = millis() + backoffMs_;
      backoffMs_ = min<uint32_t>(backoffMs_ * 2, 120000);
      return;
    }
  }
  gMqtt.loop();
  EventMsg ev;
  while (gEventQ && xQueueReceive(gEventQ, &ev, 0) == pdTRUE)
    gMqtt.publish((gBase + "event").c_str(), ev.json);
  if (metaDirty_) {
    metaDirty_ = false;
    PsDoc m, r;
    {
      StateLock l;
      cv::metaToJson(gConfig.meta, m.to<JsonObject>());
      JsonObject ro = r.to<JsonObject>();
      ro["t"] = "rules";
      cv::rulesToJson(gConfig.rules, ro["rules"].to<JsonArray>());
    }
    publish("meta", m, true);
    publish("rules", r, true);
  }
  if ((int32_t)(millis() - nextStateMs_) >= 0) {
    nextStateMs_ = millis() + gConfig.cloudIntervalSec * 1000UL;
    PsDoc doc;
    {
      StateLock l;
      cv::stateToJson(s, doc.to<JsonObject>());
    }
    addSysStats(doc.as<JsonObject>());
    publish("state", doc, true);
  }
}

// ================================================================ ntfy push
struct PushMsg {
  char title[48];
  char msg[96];
  uint8_t prio;
  char tags[32];
};
static QueueHandle_t gPushQ;

// HTTPS needs this much internal RAM in one go (much less once TLS is in PSRAM).
static uint32_t pushHeapNeed() { return tlsBlockNeed() < 30000 ? tlsBlockNeed() : 30000; }

static void pushTask(void *) {
  PushMsg m;
  for (;;) {
    beat(BEAT_PUSH);
    // Wake once a minute even with nothing to send, to keep the heartbeat.
    if (xQueueReceive(gPushQ, &m, pdMS_TO_TICKS(60000)) != pdTRUE) continue;
    for (int attempt = 0; attempt < 5; attempt++) {
      beat(BEAT_PUSH);
      if (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        continue;
      }
      // Not enough RAM for the HTTPS send. With TLS in PSRAM the cloud link
      // and a push fit side by side, so just wait a moment. Without it (the
      // classic ESP32), ask the cloud link to step aside for 30 s.
      if (ESP.getMaxAllocHeap() < pushHeapNeed()) {
        if (!gTlsInPsram) gCloud.pauseFor(30000);
        for (int i = 0; i < 50 && ESP.getMaxAllocHeap() < pushHeapNeed(); i++) vTaskDelay(pdMS_TO_TICKS(100));
        Serial.printf("[push] largest free block %u before sending\n", (unsigned)ESP.getMaxAllocHeap());
        // Still not enough: a TLS handshake now could run the heap dry
        // (2026-11-25: 792 bytes free). Wait and try again instead.
        if (ESP.getMaxAllocHeap() < pushHeapNeed()) {
          vTaskDelay(pdMS_TO_TICKS(20000));
          continue;
        }
      }
      WiFiClientSecure tls;
      tls.setCACert(ROOT_CA_PEM);
      HTTPClient http;
      http.setTimeout(10000);
      String url = String(NTFY_SERVER) + "/" + NTFY_TOPIC;
      if (!http.begin(tls, url)) break;
      http.addHeader("Title", m.title);
      http.addHeader("Priority", String(m.prio));
      http.addHeader("Tags", m.tags);
      if (strlen(NTFY_TOKEN)) http.addHeader("Authorization", String("Bearer ") + NTFY_TOKEN);
      int code = http.POST((uint8_t *)m.msg, strlen(m.msg));
      http.end();
      Serial.printf("[push] '%s' -> HTTP %d\n", m.title, code);
      if (code >= 200 && code < 300) break;
      vTaskDelay(pdMS_TO_TICKS(5000 << attempt));
    }
  }
}

void Notifier::begin() {
  if (CLOUD_ON_DISPLAY) return;  // no TLS on the hub: pushes go to the display
  gPushQ = xQueueCreate(16, sizeof(PushMsg));
  xTaskCreatePinnedToCore(pushTask, "push", 8192, nullptr, 1, nullptr, 0);
}

void Notifier::push(const char *title, const char *msg, uint8_t sev, bool cleared) {
  if (CLOUD_ON_DISPLAY) {
    // The display sends it to ntfy: {"t":"push","title":..,"msg":..,"sev":..,"clr":..}
    PsDoc d;
    d["t"] = "push";
    d["title"] = title;
    d["msg"] = msg;
    d["sev"] = sev;
    if (cleared) d["clr"] = true;
    return gLink.queueOut(d);
  }
  if (strlen(NTFY_TOPIC) == 0 || strstr(NTFY_TOPIC, "CHANGE-ME")) return;
  PushMsg m = {};
  snprintf(m.title, sizeof(m.title), "%s%s", cleared ? "Cleared: " : "", title);
  strncpy(m.msg, msg, sizeof(m.msg) - 1);
  if (cleared) {
    m.prio = 2;
    strcpy(m.tags, "white_check_mark");
  } else if (sev >= cv::SEV_CRIT) {
    m.prio = 5;
    strcpy(m.tags, "sos,rotating_light");
  } else if (sev == cv::SEV_ALARM) {
    m.prio = 5;  // "urgent": breaks through Do Not Disturb on most phones
    strcpy(m.tags, "rotating_light");
  } else if (sev == cv::SEV_WARN) {
    m.prio = 4;
    strcpy(m.tags, "warning");
  } else {
    m.prio = 3;
    strcpy(m.tags, "information_source");
  }
  xQueueSend(gPushQ, &m, 0);
}

}  // namespace hub
