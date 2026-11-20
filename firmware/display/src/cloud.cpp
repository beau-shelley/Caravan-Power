#include "cloud.h"

#include <HTTPClient.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>

#include "CaravanModel.h"
#include "../../hub/src/root_ca.h"
#include "../../hub/src/root_ca_web.h"
#include "Weather.h"

// One secrets file for both boards: the hub's.
#if __has_include("../../hub/src/secrets.h")
#include "../../hub/src/secrets.h"
#else
#warning "firmware/hub/src/secrets.h missing - the display runs without the cloud"
#include "../../hub/src/secrets.example.h"
#endif
#ifndef TZ_INFO
#define TZ_INFO "AEST-10AEDT,M10.1.0,M4.1.0/3"
#endif
#ifndef CLOUD_ON_DISPLAY
#define CLOUD_ON_DISPLAY 1
#endif

namespace cloud {

static const String kBase = String("caravan/") + DEVICE_ID + "/";

static bool mqttConfigured() { return CLOUD_ON_DISPLAY && strlen(MQTT_HOST) && !strstr(MQTT_HOST, "xxxxxxxx"); }
static bool ntfyConfigured() { return CLOUD_ON_DISPLAY && strlen(NTFY_TOPIC) && !strstr(NTFY_TOPIC, "CHANGE-ME"); }
static bool gOn = false;  // begin() set everything up
static bool wifiConfigured() { return strlen(WIFI_SSID) && strcmp(WIFI_PASS, "changeme") != 0; }

// Same networks as the hub: WIFI_SSID, then the optional WIFI_SSID2 (e.g. the
// phone hotspot in the van), tried in turn every 30 s until one connects.
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
  Serial.printf("[wifi] joining %s\n", kSsid[gNetIdx]);
}

// ---- shared between the UI task (producer) and the net task
static SemaphoreHandle_t gMux;
struct Lock {
  Lock() { xSemaphoreTake(gMux, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(gMux); }
};
static String gState, gMeta, gRules;  // latest JSON, published retained
static bool gStateNew = false, gMetaNew = false, gRulesNew = false;
static volatile bool gHubUp = false, gHubUpNew = true;
static uint32_t gLastStatePubMs = 0;

struct Out {  // non-retained publishes (resp, event)
  const char *sub;
  String *json;
};
static QueueHandle_t gOutQ, gCmdQ, gPushQ;  // Out, String*, Push
struct Push {
  char title[48];
  char msg[120];
  uint8_t sev;
  bool cleared;
};

static WiFiClientSecure gTls;
static PubSubClient gMqtt(gTls);
static volatile bool gWifiUp = false, gMqttUp = false;

bool wifiUp() { return gWifiUp; }
bool connected() { return gMqttUp; }

// ---------------------------------------------------------------- from the link
void onHubState(JsonDocument &state) {
  if (!gOn || !mqttConfigured()) return;
  // The state says whether the *hub* is on the cloud; now that's us.
  state["net"]["cloud"] = (bool)gMqttUp;
  String s;
  serializeJson(state, s);
  Lock l;
  gState = s;
  gStateNew = true;
}

void onHubMeta(JsonVariantConst meta) {
  if (!gOn || !mqttConfigured() || meta.isNull()) return;
  String s;
  serializeJson(meta, s);
  Lock l;
  if (s != gMeta) gMetaNew = true;
  gMeta = s;
}

void onHubRules(JsonVariantConst rules) {
  if (!gOn || !mqttConfigured() || rules.isNull()) return;
  JsonDocument d;  // same shape the hub published: {"t":"rules","rules":[...]}
  d["t"] = "rules";
  d["rules"] = rules;
  String s;
  serializeJson(d, s);
  Lock l;
  if (s != gRules) gRulesNew = true;
  gRules = s;
}

static void queueOut(const char *sub, const char *json, size_t len) {
  if (!gOn || !mqttConfigured()) return;
  Out o{sub, new String()};
  o.json->concat(json, len);
  if (xQueueSend(gOutQ, &o, 0) != pdTRUE) delete o.json;
}
void onHubReply(const char *json, size_t len) { queueOut("resp", json, len); }
void onHubEvent(const char *json, size_t len) {
  queueOut("event", json, len);
  if (!gOn) return;
  Lock l;
  gLastStatePubMs = 0;  // alerts changed: publish the next state straight away
}

void onHubPush(JsonObjectConst p) {
  if (!gOn || !ntfyConfigured()) return;
  Push m = {};
  strlcpy(m.title, p["title"] | "Caravan", sizeof(m.title));
  strlcpy(m.msg, p["msg"] | "", sizeof(m.msg));
  m.sev = p["sev"] | 1;
  m.cleared = p["clr"] | false;
  xQueueSend(gPushQ, &m, 0);
}

void onHubLink(bool up) {
  if (up != gHubUp) {
    gHubUp = up;
    gHubUpNew = true;
  }
}

// ---------------------------------------------------------------- forecast
// A hub without Wi-Fi asks us ({"t":"wx_req","lat","lon"}); the cloud task
// fetches Open-Meteo and the answer goes back as {"c":"wx_set","wx":{...}}.
static volatile bool gWxWanted = false;
static float gWxLat = 0, gWxLon = 0;
static QueueHandle_t gLocalQ;  // String*: commands of our own for the hub
static String gWxLast;          // last good {"c":"wx_set",...}, re-sent after a hub restart
static uint32_t gWxLastMs = 0;

static void queueLocal(const String &json) {
  String *s = new String(json);
  if (xQueueSend(gLocalQ, &s, 0) != pdTRUE) delete s;
}

void resendWeather() {
  if (!gOn) return;
  Lock l;
  if (gWxLast.length() && millis() - gWxLastMs < 3UL * 3600 * 1000) queueLocal(gWxLast);
}

void requestWeather(float lat, float lon) {
  if (!gOn) return;
  Lock l;
  // Same place, fetched under 20 min ago: answer from memory straight away.
  if (gWxLast.length() && millis() - gWxLastMs < 20UL * 60 * 1000 && fabsf(lat - gWxLat) < 0.05f &&
      fabsf(lon - gWxLon) < 0.05f) {
    queueLocal(gWxLast);
    return;
  }
  gWxLat = lat;
  gWxLon = lon;
  gWxWanted = true;
}

static void fetchWeather() {
  float lat, lon;
  {
    Lock l;
    if (!gWxWanted) return;
    gWxWanted = false;
    lat = gWxLat;
    lon = gWxLon;
  }
  char path[400];
  cv::openMeteoPath(path, sizeof(path), lat, lon);
  WiFiClientSecure tls;
  tls.setCACert(WEB_ROOTS_PEM);
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(tls, String("https://api.open-meteo.com") + path)) return;
  int code = http.GET();
  char err[100] = "";
  if (code < 0) tls.lastError(err, sizeof(err));
  String body = code == 200 ? http.getString() : String();
  http.end();
  cv::Weather w;
  if (code != 200 || !cv::parseOpenMeteo(body.c_str(), body.length(), w)) {
    Serial.printf("[weather] fetch failed (HTTP %d, TLS '%s', internal RAM %u free, largest %u, PSRAM %u) - the hub asks again in 5 min\n",
                  code, err, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return;
  }
  w.updated = (uint32_t)time(nullptr);
  JsonDocument d;
  d["c"] = "wx_set";
  cv::weatherToJson(w, d["wx"].to<JsonObject>());
  String js;
  serializeJson(d, js);
  {
    Lock l;
    gWxLast = js;
    gWxLastMs = millis();
    queueLocal(js);
  }
  Serial.printf("[weather] %.1f C, %s - sent to the hub\n", w.temp, cv::wxText(w.code));
}

bool takeLocalCommand(JsonDocument &cmd) {
  String *s = nullptr;
  if (!gOn || xQueueReceive(gLocalQ, &s, 0) != pdTRUE) return false;
  bool ok = !deserializeJson(cmd, *s);
  delete s;
  return ok;
}

bool takeCommand(JsonDocument &cmd) {
  String *s = nullptr;
  if (!gOn || xQueueReceive(gCmdQ, &s, 0) != pdTRUE) return false;
  bool ok = !deserializeJson(cmd, *s);
  delete s;
  if (!ok || !cmd["c"].is<const char *>()) return false;
  cmd["via"] = "cloud";  // the hub applies the remote-control setting to it
  return true;
}

// ---------------------------------------------------------------- MQTT
static void onMqtt(char *topic, byte *payload, unsigned int len) {
  if (String(topic) != kBase + "cmd") return;
  String *s = new String();
  s->concat((const char *)payload, len);
  if (xQueueSend(gCmdQ, &s, 0) != pdTRUE) delete s;
}

static uint32_t gNextTryMs = 0, gBackoffMs = 2000, gConnFails = 0;

static void mqttLoop() {
  if (!mqttConfigured() || !gWifiUp) {
    gMqttUp = false;
    return;
  }
  if (!gMqtt.connected()) {
    gMqttUp = false;
    if ((int32_t)(millis() - gNextTryMs) < 0) return;
    String will = kBase + "online";
    String cid = String(DEVICE_ID) + "-display-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    IPAddress ip;
    bool dnsOk = WiFi.hostByName(MQTT_HOST, ip);
    Serial.printf("[cloud] connecting to %s:%d (%s)\n", MQTT_HOST, MQTT_PORT,
                  dnsOk ? ip.toString().c_str() : "DNS failed");
    if (gMqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS, will.c_str(), 1, true, "0")) {
      Serial.println("[cloud] connected");
      gBackoffMs = 2000;
      gConnFails = 0;
      gMqtt.subscribe((kBase + "cmd").c_str(), 1);
      gHubUpNew = true;
      Lock l;
      gMetaNew = gMeta.length() > 0;
      gRulesNew = gRules.length() > 0;
      gLastStatePubMs = 0;
    } else {
      // rc 4 = bad username/password, 5 = not authorised, -2 = network / TLS
      Serial.printf("[cloud] failed rc=%d, retry in %us\n", gMqtt.state(), gBackoffMs / 1000);
      gTls.stop();  // never reuse a half-open socket
      // 2026-11-20: after one TLS send error on Starlink every later connect
      // timed out (errno 113) while the laptop on the same Wi-Fi connected in
      // 0.2 s. A stale Wi-Fi/IP state; rejoining the same network clears it.
      if (++gConnFails >= 3) {
        gConnFails = 0;
        Serial.println("[cloud] 3 failed connects in a row: rejoining Wi-Fi");
        WiFi.disconnect();
        joinNet(gNetIdx);
        gBackoffMs = 2000;
      }
      gNextTryMs = millis() + gBackoffMs;
      gBackoffMs = min<uint32_t>(gBackoffMs * 2, 120000);
      return;
    }
  }
  gMqttUp = true;
  gMqtt.loop();

  if (gHubUpNew) {  // "online" = the hub's data is flowing, as before
    gHubUpNew = false;
    gMqtt.publish((kBase + "online").c_str(), gHubUp ? "1" : "0", true);
  }
  Out o;
  while (xQueueReceive(gOutQ, &o, 0) == pdTRUE) {
    gMqtt.publish((kBase + o.sub).c_str(), (const uint8_t *)o.json->c_str(), o.json->length(), false);
    delete o.json;
  }
  String meta, rules, state;
  {
    Lock l;
    if (gMetaNew) meta = gMeta;
    if (gRulesNew) rules = gRules;
    gMetaNew = gRulesNew = false;
    // Every 15 s (the hub's default cloudIntervalSec): ~5 MB a day.
    if (gStateNew && (!gLastStatePubMs || millis() - gLastStatePubMs >= 15000)) {
      state = gState;
      gStateNew = false;
      gLastStatePubMs = millis() | 1;
    }
  }
  auto pub = [](const char *sub, const String &s) {
    if (s.length()) gMqtt.publish((kBase + sub).c_str(), (const uint8_t *)s.c_str(), s.length(), true);
  };
  pub("meta", meta);
  pub("rules", rules);
  pub("state", state);
}

// ---------------------------------------------------------------- ntfy
static void sendPush(const Push &m) {
  char title[64];
  snprintf(title, sizeof(title), "%s%s", m.cleared ? "Cleared: " : "", m.title);
  int prio;
  const char *tags;
  if (m.cleared) prio = 2, tags = "white_check_mark";
  else if (m.sev >= cv::SEV_CRIT) prio = 5, tags = "sos,rotating_light";
  else if (m.sev == cv::SEV_ALARM) prio = 5, tags = "rotating_light";  // breaks through Do Not Disturb
  else if (m.sev == cv::SEV_WARN) prio = 4, tags = "warning";
  else prio = 3, tags = "information_source";

  for (int attempt = 0; attempt < 3; attempt++) {  // runs on the cloud task: keep retries short
    if (!gWifiUp) return;
    WiFiClientSecure tls;
    tls.setCACert(ROOT_CA_PEM);
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(tls, String(NTFY_SERVER) + "/" + NTFY_TOPIC)) return;
    http.addHeader("Title", title);
    http.addHeader("Priority", String(prio));
    http.addHeader("Tags", tags);
    if (strlen(NTFY_TOKEN)) http.addHeader("Authorization", String("Bearer ") + NTFY_TOKEN);
    int code = http.POST((uint8_t *)m.msg, strlen(m.msg));
    char err[100] = "";
    if (code < 0) tls.lastError(err, sizeof(err));
    http.end();
    Serial.printf("[push] '%s' -> HTTP %d %s\n", title, code, err);
    if (code >= 200 && code < 300) return;
    vTaskDelay(pdMS_TO_TICKS(5000 << attempt));
  }
}


// ---------------------------------------------------------------- Wi-Fi + task
static void netTask(void *) {
  uint32_t lastJoinMs = 0;
  for (;;) {
    bool up = WiFi.status() == WL_CONNECTED;
    if (up != gWifiUp) {
      gWifiUp = up;
      if (up) Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
      static bool ntpStarted = false;
      if (up && !ntpStarted) {
        // Clock from NTP: shown on screen and passed to the hub (no Wi-Fi there).
        // Started here, on this task, once Wi-Fi is up, and by IP address
        // (Google's anycast time servers): SNTP looking up a host name while
        // the MQTT connect resolved the broker crashed the display with an
        // lwIP "Required to lock TCPIP core" assert (2026-11-20).
        ntpStarted = true;
        configTzTime(TZ_INFO, "216.239.35.0", "216.239.35.4");
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
      else Serial.println("[wifi] disconnected");
    }
    if (!up && millis() - lastJoinMs > 30000) {  // auto-reconnect is on; this is the backstop
      lastJoinMs = millis();
      WiFi.disconnect();
      joinNet(gNetIdx + 1);  // the other network, if there is one
    }
    // One TLS session at a time: with the MQTT link open there is only ~13 KB
    // of internal RAM left (largest block ~5 KB), too little for a second
    // handshake (2026-11-20: every forecast fetch failed, HTTP -1). So push
    // alerts and the forecast pause MQTT for the few seconds they take.
    bool wantPush = ntfyConfigured() && uxQueueMessagesWaiting(gPushQ) > 0;
    bool wantWx = gWxWanted && time(nullptr) > 1700000000;
    if (up && (wantPush || wantWx)) {
      if (gMqtt.connected()) {
        gMqtt.disconnect();
        gTls.stop();
        gMqttUp = false;
        Serial.println("[cloud] MQTT paused for a push alert / forecast");
      }
      Push m;
      while (xQueueReceive(gPushQ, &m, 0) == pdTRUE) sendPush(m);
      if (wantWx) fetchWeather();
      gNextTryMs = 0;  // reconnect straight away
      gHubUpNew = true;
    }
    mqttLoop();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// mbedTLS takes its ~40 KB per connection from internal RAM, which on this
// board is mostly spoken for (Wi-Fi, the RGB bounce buffers): the forecast
// failed with "SSL - Memory allocation failed" with 69 KB free but no block
// over 24 KB (2026-11-20), while 2.7 MB of PSRAM sat idle. Use PSRAM first.
static void *tlsCalloc(size_t n, size_t size) {
  void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void begin() {
#if defined(MBEDTLS_PLATFORM_MEMORY) && !defined(MBEDTLS_PLATFORM_CALLOC_MACRO)
  mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free);
#else
#warning "mbedTLS allocator can't be changed at run time - TLS buffers stay in internal RAM"
#endif
  // Wi-Fi alone is enough: the clock and the forecast for the hub need it even
  // with no broker or ntfy topic set (those are checked where they're used).
  if (!wifiConfigured()) {
    Serial.println("[cloud] off (no Wi-Fi set in secrets.h)");
    return;
  }
  gMux = xSemaphoreCreateMutex();
  gOutQ = xQueueCreate(16, sizeof(Out));
  gCmdQ = xQueueCreate(8, sizeof(String *));
  gPushQ = xQueueCreate(16, sizeof(Push));
  gLocalQ = xQueueCreate(4, sizeof(String *));

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME "-display");
  WiFi.setAutoReconnect(true);
  joinNet(0);

  gTls.setCACert(ROOT_CA_PEM);
  gMqtt.setServer(MQTT_HOST, MQTT_PORT);
  gMqtt.setBufferSize(8192);  // a full state is ~1.5 KB, rules a few KB
  gMqtt.setKeepAlive(60);     // Starlink handovers can stall a few seconds
  gMqtt.setSocketTimeout(10);
  gMqtt.setCallback(onMqtt);

  gOn = true;
  xTaskCreatePinnedToCore(netTask, "cloud", 8192, nullptr, 1, nullptr, 0);
  Serial.printf("[cloud] on: MQTT %s, push alerts %s\n", mqttConfigured() ? MQTT_HOST : "off",
                ntfyConfigured() ? "on" : "off");
}

}  // namespace cloud
