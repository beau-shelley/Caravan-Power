// Everything that talks to the outside: RS485 display link, local web UI +
// WebSocket, cloud MQTT (TLS) and phone push notifications (ntfy).
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "CaravanModel.h"

namespace hub {

// The network task (cloud/web) and the main loop share the state; hold this
// while reading or writing gState / gConfig from either side.
extern SemaphoreHandle_t gStateMux;
struct StateLock {
  StateLock() { xSemaphoreTakeRecursive(gStateMux, portMAX_DELAY); }
  ~StateLock() { xSemaphoreGiveRecursive(gStateMux); }
};

enum Source : uint8_t { SRC_DISPLAY = 0, SRC_LOCAL_WEB = 1, SRC_CLOUD = 2 };

// Implemented in main.cpp. Fills `reply` (may stay empty).
void handleCommand(JsonObjectConst cmd, Source src, JsonDocument &reply);
// Health figures for a soak test, added to every state message as "sys":
// uptime, why it last restarted, lowest free / largest-block RAM since boot,
// and how many times the cloud link has (re)connected.
void addSysStats(JsonObject o);
void healthBegin();  // at boot: load the previous run's figures, count the boot
void healthLoop();   // every loop: saves this run's figures every 10 min

// ---------------------------------------------------------------- RS485 link
class DisplayLink {
 public:
  void begin(HardwareSerial &ser, int rx, int tx, int de, int re = -1);
  void loop(const cv::State &s);
  void send(JsonDocument &doc);
  void requestStateSoon() { nextStateMs_ = 0; }
  void pushConfigSoon() { configDirty_ = true; }  // names/rules changed elsewhere
  // Queue a message for the display from another task (hold StateLock).
  void queueReply(const String &json) { pending_ = json; }
  bool displayOnline() const { return lastRxMs_ && millis() - lastRxMs_ < 10000; }
  // Queue an alert event / push alert for the display to send on (any task).
  void queueOut(JsonDocument &doc);

 private:
  HardwareSerial *ser_ = nullptr;
  int de_ = -1;
  int re_ = -1;  // separate /RE pin, driven together with DE (-1 = bridged to DE or auto)
  void txEnable(bool on);
  uint32_t nextStateMs_ = 0, nextPollMs_ = 0, lastRxMs_ = 0;
  volatile bool configDirty_ = false;
  String pending_;
  struct LinkOut {
    char json[320];
  };
  QueueHandle_t outQ_ = nullptr;
  void write(const char *buf, size_t n);
};

// ---------------------------------------------------------------- local web
class LocalWeb {
 public:
  void begin();
  void loop(const cv::State &s);
  void broadcast(JsonDocument &doc);
  void broadcastStateSoon() { nextPushMs_ = 0; }

 private:
  uint32_t nextPushMs_ = 0;
};

// ---------------------------------------------------------------- cloud MQTT
class Cloud {
 public:
  void begin();
  void loop(const cv::State &s);
  void publish(const char *sub, JsonDocument &doc, bool retain = false);
  // Thread-safe: queue an event from any task; sent by the network task.
  void queueEvent(JsonDocument &doc);
  void publishStateSoon() { nextStateMs_ = 0; }
  void publishMetaSoon() { metaDirty_ = true; }
  bool connected();
  // Push alerts come first: with the classic ESP32's RAM, an alert's HTTPS
  // send and the cloud link's TLS session don't fit together. The push task
  // asks the cloud link to step aside for a while; it reconnects afterwards.
  void pauseFor(uint32_t ms) { pauseUntilMs_ = millis() + ms; pauseReq_ = true; }

 private:
  volatile bool pauseReq_ = false;
  volatile uint32_t pauseUntilMs_ = 0;
  uint32_t nextStateMs_ = 0, nextTryMs_ = 0;
  bool metaDirty_ = true;
  uint32_t backoffMs_ = 2000;
};

// ---------------------------------------------------------------- push alerts
class Notifier {
 public:
  void begin();
  // Queue a phone notification (non-blocking; sent from a background task).
  void push(const char *title, const char *msg, uint8_t sev, bool cleared = false);
};

// ---------------------------------------------------------------- Wi-Fi + time
class Net {
 public:
  void begin();
  void beginOffline();  // Wi-Fi off build (HUB_WIFI=0)
  bool offline() const { return offline_; }
  void loop(cv::State &s);
  bool timeValid() const;
  // Network watchdog, run from the net task: pings the router every 30 s. If
  // Wi-Fi says it's connected but nothing gets through for 90 s, it rejoins
  // Wi-Fi, and if that doesn't help, restarts the hub (secure mode re-arms itself).
  void watchdog();
  // Heartbeat from the net task; loop() restarts the hub if it stops (a wedged web server).
  void netAlive() { netAliveMs_ = millis(); }

 private:
  volatile uint32_t netAliveMs_ = 0;
  bool offline_ = false;
  uint32_t nextPingMs_ = 30000, lastPingOkMs_ = 0, rejoinedMs_ = 0, pingDoneMs_ = 0;
  void *ping_ = nullptr;  // esp_ping_handle_t while a ping round runs
};

extern DisplayLink gLink;
extern LocalWeb gWeb;
extern Cloud gCloud;
extern Notifier gNotify;
extern Net gNet;

}  // namespace hub
