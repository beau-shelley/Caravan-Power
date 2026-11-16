#include "Comms.h"

#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <lwip/sockets.h>
#include <ping/ping_sock.h>
#include <lwip/ip_addr.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_random.h>

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

// ================================================================ Net
void Net::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
  Serial.printf("[net] connecting to '%s'\n", WIFI_SSID);
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
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    } else {
      Serial.println("[net] network still jammed after rejoining - restarting");
      delay(100);
      ESP.restart();
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
  if (netAliveMs_ && millis() - netAliveMs_ > 120000) {
    Serial.printf("[net] net task stuck for 2 min (heap %u, min %u) - restarting\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMinFreeHeap());
    delay(100);
    ESP.restart();
  }
  static uint32_t nextHeapLog = 0;
  if ((int32_t)(millis() - nextHeapLog) >= 0) {
    nextHeapLog = millis() + 30000;
    Serial.printf("[sys] heap %u free, min %u, largest %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
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
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !was) {
    Serial.printf("[net] Wi-Fi up, IP %s (heap %u free, largest %u)\n", WiFi.localIP().toString().c_str(),
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    MDNS.begin(HOSTNAME);
    MDNS.addService("http", "tcp", 80);
  }
  if (!up && millis() - lastTry > 30000) {  // belt and braces
    lastTry = millis();
    WiFi.reconnect();
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
// Half duplex: the hub is bus master. It sends a state frame every second and
// a tiny poll frame every 250 ms; the display only transmits right after it
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
    JsonDocument in;
    if (deserializeJson(in, reader.line(), reader.length())) continue;
    if (in["c"].is<const char *>()) {
      // "via":"cloud" = the display relaying a command from the app / web page
      // through the cloud broker: same rules as the hub's own MQTT link.
      bool cloud = !strcmp(in["via"] | "", "cloud");
      JsonDocument reply;
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
    nextPollMs_ = now + 250;
  } else if (outQ_ && xQueueReceive(outQ_, &out, 0) == pdTRUE) {  // alert events / pushes for the cloud
    write(out.json, strlen(out.json));
    nextPollMs_ = now + 250;
  } else if (configDirty_) {
    configDirty_ = false;
    JsonDocument doc;
    JsonDocument cmd;
    cmd["c"] = "hello";
    handleCommand(cmd.as<JsonObjectConst>(), SRC_DISPLAY, doc);
    send(doc);
    nextPollMs_ = now + 250;
  } else if ((int32_t)(now - nextStateMs_) >= 0) {
    nextStateMs_ = now + 1000;
    nextPollMs_ = now + 250;
    JsonDocument doc;
    cv::stateToJson(s, doc.to<JsonObject>());
    send(doc);
  } else if ((int32_t)(now - nextPollMs_) >= 0) {
    nextPollMs_ = now + 250;
    if (s.lvl.online) {
      // The poll slot carries the level reading, so the touchscreen's level
      // page updates 4x a second instead of once.
      JsonDocument doc;
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
    JsonDocument in;
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
    JsonDocument reply;
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
    JsonDocument d;
    d["tok"] = gWsToken;
    d["wsPort"] = 81;
    sendJson(d);
  });
  gHttp.on("/api/state", HTTP_GET, [] {
    if (!checkAuth()) return;
    JsonDocument reply, cmd;
    cmd["c"] = "state";
    handleCommand(cmd.as<JsonObjectConst>(), SRC_LOCAL_WEB, reply);
    sendJson(reply);
  });
  gHttp.on("/api/cmd", HTTP_POST, [] {
    if (!checkAuth()) return;
    JsonDocument in, reply;
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
    JsonDocument d;
    {
      StateLock l;
      gConfig.toJson(d.to<JsonObject>());
    }
    sendJson(d);
  });
  gHttp.on("/api/config", HTTP_POST, [] {
    if (!checkAuth()) return;
    JsonDocument in;
    if (deserializeJson(in, gHttp.arg("plain"))) {
      gHttp.send(400, "text/plain", "bad json");
      return;
    }
    JsonDocument cmd, reply;
    cmd["c"] = "config_set";
    cmd["cfg"] = in.as<JsonObjectConst>();
    handleCommand(cmd.as<JsonObjectConst>(), SRC_LOCAL_WEB, reply);
    sendJson(reply);
  });
  gHttp.on("/api/bmpro/log", HTTP_GET, [] {
    if (!checkAuth()) return;
    JsonDocument cmd, reply;
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
    JsonDocument doc;
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
  JsonDocument in, reply;
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
    String will = gBase + "online";
    String cid = String(DEVICE_ID) + "-hub-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.printf("[cloud] connecting to %s:%d\n", MQTT_HOST, MQTT_PORT);
    if (gMqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS, will.c_str(), 1, true, "0")) {
      Serial.println("[cloud] connected");
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
    JsonDocument m, r;
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
    JsonDocument doc;
    {
      StateLock l;
      cv::stateToJson(s, doc.to<JsonObject>());
    }
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

static constexpr uint32_t kPushHeapNeed = 30000;

static void pushTask(void *) {
  PushMsg m;
  for (;;) {
    if (xQueueReceive(gPushQ, &m, portMAX_DELAY) != pdTRUE) continue;
    for (int attempt = 0; attempt < 5; attempt++) {
      if (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        continue;
      }
      // HTTPS needs ~35 KB in one go. If the cloud link holds it, ask it to
      // step aside (it comes back by itself) and wait up to 5 s for the RAM.
      if (ESP.getMaxAllocHeap() < kPushHeapNeed) {
        gCloud.pauseFor(30000);
        for (int i = 0; i < 50 && ESP.getMaxAllocHeap() < kPushHeapNeed; i++) vTaskDelay(pdMS_TO_TICKS(100));
        Serial.printf("[push] largest free block %u before sending\n", (unsigned)ESP.getMaxAllocHeap());
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
    JsonDocument d;
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
