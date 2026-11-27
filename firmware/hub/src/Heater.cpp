#include "Heater.h"

#include <NimBLEDevice.h>
#include "BleCompat.h"

#include "Ble.h"
#include "Comms.h"

namespace hub {

HeaterBle gHeaterBle;
Heater433 gHeater433;

namespace {
constexpr uint32_t POLL_MS = 2000;     // ask for status this often (the app does ~1 s)
constexpr uint32_t STALE_MS = 15000;   // no status for this long = offline
constexpr int LOG_LINES = 60;

SemaphoreHandle_t gMux = nullptr;
bool gTaskStarted = false, gListening = false;
char gAddr[18] = {};
uint16_t gPass = cv::airheater::DEFAULT_PASSKEY;
volatile bool gRetarget = false;

NimBLEClient *gClient = nullptr;
NimBLERemoteCharacteristic *gChr = nullptr;

cv::Heater gStatus;  // decoded from the last notification (under gMux)
uint32_t gStatusMs = 0;

String gLog[LOG_LINES];
int gLogHead = 0, gLogCount = 0;

struct Seen {
  char addr[18];
  char name[24];
  int rssi;
  uint32_t lastMs;
};
constexpr int MAX_SEEN = 8;
Seen gSeen[MAX_SEEN];
int gSeenCount = 0;

struct Frame {
  uint8_t b[cv::airheater::CMD_LEN];
};
QueueHandle_t gTxQ = nullptr;

String hex(const uint8_t *d, size_t n) {
  String s;
  char b[4];
  for (size_t i = 0; i < n; i++) {
    snprintf(b, sizeof(b), "%02X ", d[i]);
    s += b;
  }
  return s;
}

void logLine(const String &line) {
  xSemaphoreTake(gMux, portMAX_DELAY);
  gLog[gLogHead] = String(millis() / 1000) + "s " + line;
  gLogHead = (gLogHead + 1) % LOG_LINES;
  if (gLogCount < LOG_LINES) gLogCount++;
  xSemaphoreGive(gMux);
  Serial.println("[heater] " + line);
}

// Anything advertising the FFE0 service, or with "heater" in its name.
void onAdvert(const ble::Advert &a) {
  bool looks = ble::containsNoCase(a.name, "heater") ||
               a.dev->isAdvertisingService(NimBLEUUID(cv::airheater::SERVICE_UUID));
  if (!looks) return;
  xSemaphoreTake(gMux, portMAX_DELAY);
  int slot = -1;
  for (int i = 0; i < gSeenCount; i++)
    if (!strcmp(a.addr, gSeen[i].addr)) slot = i;
  if (slot < 0 && gSeenCount < MAX_SEEN) slot = gSeenCount++;
  if (slot >= 0) {
    snprintf(gSeen[slot].addr, sizeof(gSeen[slot].addr), "%s", a.addr);
    if (a.name[0]) snprintf(gSeen[slot].name, sizeof(gSeen[slot].name), "%s", a.name);
    gSeen[slot].rssi = a.rssi;
    gSeen[slot].lastMs = millis();
  }
  xSemaphoreGive(gMux);
}

void onNotify(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
  static cv::Heater h;  // static: the NimBLE host task has a small stack
  xSemaphoreTake(gMux, portMAX_DELAY);
  h = gStatus;
  xSemaphoreGive(gMux);
  bool ok = cv::airheater::decode(data, len, h);
  // Every distinct frame goes in the log so the protocol can be checked.
  static uint8_t last[32];
  static size_t lastLen = 0;
  if (len != lastLen || memcmp(last, data, len < sizeof(last) ? len : sizeof(last))) {
    logLine(String(ok ? "RX " : "RX (not decoded) ") + hex(data, len));
    lastLen = len;
    memcpy(last, data, len < sizeof(last) ? len : sizeof(last));
  }
  if (!ok) return;
  xSemaphoreTake(gMux, portMAX_DELAY);
  gStatus = h;
  gStatusMs = millis();
  xSemaphoreGive(gMux);
}

bool write(const uint8_t *b) {
  if (!gChr) return false;
  return gChr->writeValue(b, cv::airheater::CMD_LEN, !gChr->canWriteNoResponse());
}

void connectNow() {
  if (!gClient) {
    gClient = NimBLEDevice::createClient();
    blec::setConnectTimeoutSec(gClient, 8);
  }
  gChr = nullptr;
  logLine(String("connecting to ") + gAddr);
  ble::pauseScan();
  bool ok = gClient->connect(blec::makeAddr(std::string(gAddr)));
  ble::resumeScan();
  if (!ok) {
    logLine("connect failed - retrying in 15 s");
    return;
  }
  NimBLERemoteService *svc = gClient->getService(NimBLEUUID(cv::airheater::SERVICE_UUID));
  NimBLERemoteCharacteristic *c = svc ? svc->getCharacteristic(NimBLEUUID(cv::airheater::CHAR_UUID)) : nullptr;
  if (!c) {
    // Not the expected layout: list what it does have, then give up on it.
    logLine("no FFE0/FFE1 - this heater uses another protocol. Its services:");
    for (auto *s : blec::services(gClient, true)) {
      logLine(String("  SVC ") + s->getUUID().toString().c_str());
      for (auto *ch : blec::chars(s, true))
        logLine(String("    CHR ") + ch->getUUID().toString().c_str() + (ch->canNotify() ? " notify" : "") +
                (ch->canWrite() || ch->canWriteNoResponse() ? " write" : ""));
    }
    gClient->disconnect();
    return;
  }
  if (c->canNotify()) c->subscribe(true, onNotify);
  else if (c->canIndicate()) c->subscribe(false, onNotify);
  gChr = c;
  logLine("connected");
}

void task(void *) {
  uint32_t nextTry = 0, nextPoll = 0;
  uint8_t poll[cv::airheater::CMD_LEN];
  for (;;) {
    beat(BEAT_HEATER);
    if (gRetarget) {
      gRetarget = false;
      if (gClient && gClient->isConnected()) gClient->disconnect();
      gChr = nullptr;
      nextTry = 0;
    }
    bool up = gClient && gClient->isConnected() && gChr;
    if (!up && gAddr[0] && (int32_t)(millis() - nextTry) >= 0) {
      nextTry = millis() + 15000;
      // One connect at a time (the BMPRO task pauses the same scan).
      if (ble::lockConnect()) {
        connectNow();
        ble::unlockConnect();
      }
      up = gClient && gClient->isConnected() && gChr;
    }
    if (up) {
      Frame f;
      bool sent = false;
      while (xQueueReceive(gTxQ, &f, 0) == pdTRUE) {
        bool ok = write(f.b);
        logLine(String("TX ") + hex(f.b, sizeof(f.b)) + (ok ? "" : " FAILED"));
        sent = true;
        vTaskDelay(pdMS_TO_TICKS(150));  // let it take each frame in turn
      }
      if (sent) nextPoll = 0;  // show the result straight away
      if ((int32_t)(millis() - nextPoll) >= 0) {
        nextPoll = millis() + POLL_MS;
        cv::airheater::encode(cv::airheater::CMD_STATUS, 0, gPass, poll);
        write(poll);
      }
    } else {
      xQueueReset(gTxQ);  // don't replay stale commands on reconnect
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void startTask() {
  if (gTaskStarted) return;
  gTaskStarted = true;
  if (!gMux) gMux = xSemaphoreCreateMutex();
  gTxQ = xQueueCreate(8, sizeof(Frame));
  ble::begin();
  ble::addListener(onAdvert);
  xTaskCreatePinnedToCore(task, "heater", 6144, nullptr, 1, nullptr, 0);
}
}  // namespace

void HeaterBle::setTarget(const char *addr, uint16_t passkey) {
  startTask();
  snprintf(gAddr, sizeof(gAddr), "%s", addr ? addr : "");
  gPass = passkey;
  gRetarget = true;
  if (!gAddr[0]) listen();
  logLine(String("target '") + gAddr + "'");
}

void HeaterBle::release() {
  if (!gTaskStarted || !gAddr[0]) return;
  gAddr[0] = 0;
  gRetarget = true;
  logLine("released");
}

void HeaterBle::listen() {
  startTask();
  if (!gListening) {
    gListening = true;
    ble::keepScanning();
  }
}

bool HeaterBle::connected() const { return gClient && gClient->isConnected() && gChr; }

void HeaterBle::loop(cv::State &s) {
  if (!gMux) {
    s.htr.online = false;
    return;
  }
  uint8_t link = s.htr.link;
  xSemaphoreTake(gMux, portMAX_DELAY);
  bool fresh = gStatusMs && millis() - gStatusMs < STALE_MS;
  if (fresh) s.htr = gStatus;
  xSemaphoreGive(gMux);
  s.htr.link = link;
  s.htr.online = fresh && connected();
}

bool HeaterBle::send(const cv::HeaterCmd &c, const cv::Heater &cur, const char **why) {
  if (!gAddr[0]) {
    if (why) *why = "Heater's Bluetooth address isn't set - Setup on the phone";
    return false;
  }
  if (!connected()) {
    if (why) *why = "Not connected to the heater yet";
    return false;
  }
  uint8_t frames[4][cv::airheater::CMD_LEN];
  int n = cv::airheater::framesFor(c, cur, gPass, frames, 4);
  for (int i = 0; i < n; i++) {
    Frame f;
    memcpy(f.b, frames[i], sizeof(f.b));
    if (xQueueSend(gTxQ, &f, 0) != pdTRUE) {
      if (why) *why = "Heater busy - try again";
      return false;
    }
  }
  return true;
}

String HeaterBle::seen() const {
  if (!gMux) return "[]";
  String out = "[";
  xSemaphoreTake(gMux, portMAX_DELAY);
  for (int i = 0; i < gSeenCount; i++) {
    if (i) out += ",";
    out += String("{\"addr\":\"") + gSeen[i].addr + "\",\"name\":\"" + gSeen[i].name + "\",\"rssi\":" + gSeen[i].rssi +
           ",\"age\":" + (millis() - gSeen[i].lastMs) / 1000 + "}";
  }
  xSemaphoreGive(gMux);
  return out + "]";
}

String HeaterBle::log() const {
  if (!gMux) return "Heater Bluetooth not started (set the heater up in Setup first)";
  String out;
  xSemaphoreTake(gMux, portMAX_DELAY);
  int start = (gLogHead - gLogCount + LOG_LINES) % LOG_LINES;
  for (int i = 0; i < gLogCount; i++) out += gLog[(start + i) % LOG_LINES] + "\n";
  xSemaphoreGive(gMux);
  return out;
}

// ---------------------------------------------------------------- 433 MHz stub
// TODO when needed: CC1101 on SPI (GDO0 to a spare GPIO), capture the remote's
// on/off/up/down codes in receive mode, store them in the config and replay
// them here. One-way: the heater never reports back, so the screens show the
// state the hub last sent and no readings.
void Heater433::loop(cv::State &s) { s.htr.online = false; }

bool Heater433::send(const cv::HeaterCmd &c, const cv::Heater &cur, const char **why) {
  (void)c;
  (void)cur;
  if (why) *why = "433 MHz heater remote isn't built yet (needs a CC1101 module) - use Bluetooth";
  return false;
}

}  // namespace hub
