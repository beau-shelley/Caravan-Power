#include "Bmpro.h"

#include <NimBLEDevice.h>

namespace hub {

Bmpro gBmpro;

namespace {
constexpr int LOG_LINES = 120;
String gLog[LOG_LINES];
int gLogHead = 0, gLogCount = 0;
SemaphoreHandle_t gMux;

struct Seen {
  char addr[18];
  char name[24];
  int rssi;
  uint32_t lastMs;
};
constexpr int MAX_SEEN = 24;
Seen gSeen[MAX_SEEN];
int gSeenCount = 0;

NimBLEClient *gClient = nullptr;
char gTargetAddr[18] = {};
volatile bool gWantConnect = false;

// Latest decoded data handed from the BLE task to loop().
cv::State gPending;
volatile bool gPendingValid = false;

void logLine(const String &s) {
  xSemaphoreTake(gMux, portMAX_DELAY);
  gLog[gLogHead] = String(millis() / 1000) + "s " + s;
  gLogHead = (gLogHead + 1) % LOG_LINES;
  if (gLogCount < LOG_LINES) gLogCount++;
  xSemaphoreGive(gMux);
  Serial.println("[bmpro] " + s);
}

String hex(const uint8_t *d, size_t n) {
  String s;
  s.reserve(n * 3);
  char b[4];
  for (size_t i = 0; i < n; i++) {
    sprintf(b, "%02X ", d[i]);
    s += b;
  }
  return s;
}

class ScanCb : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *d) override {
    std::string addr = d->getAddress().toString();
    std::string name = d->haveName() ? d->getName() : "";
    xSemaphoreTake(gMux, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < gSeenCount; i++)
      if (addr == gSeen[i].addr) slot = i;
    bool fresh = slot < 0;
    if (fresh && gSeenCount < MAX_SEEN) slot = gSeenCount++;
    if (slot >= 0) {
      strncpy(gSeen[slot].addr, addr.c_str(), 17);
      if (!name.empty()) strncpy(gSeen[slot].name, name.c_str(), 23);
      gSeen[slot].rssi = d->getRSSI();
      gSeen[slot].lastMs = millis();
    }
    xSemaphoreGive(gMux);
    // Log adverts from anything that looks like BMPRO (name match) in full -
    // some devices broadcast live data in manufacturer data.
    String n = String(name.c_str());
    n.toUpperCase();
    if (n.indexOf("BMPRO") >= 0 || n.indexOf("COMMLINK") >= 0 || n.indexOf("TREK") >= 0 ||
        addr == gTargetAddr) {
      std::string md = d->getManufacturerData();
      logLine(String("ADV ") + addr.c_str() + " '" + name.c_str() + "' rssi " + d->getRSSI() +
              " mfg: " + hex((const uint8_t *)md.data(), md.size()));
    }
  }
};

void onNotify(NimBLERemoteCharacteristic *c, uint8_t *data, size_t len, bool isNotify) {
  std::string uuid = c->getUUID().toString();
  logLine(String(isNotify ? "NTF " : "IND ") + uuid.c_str() + " [" + len + "] " + hex(data, len));
  static cv::State tmp;  // static: NimBLE host task has a small stack
  tmp = gPending;
  if (Bmpro::decode(uuid.c_str(), data, len, tmp)) {
    gPending = tmp;
    gPendingValid = true;
  }
}

void dumpAndSubscribe() {
  std::vector<NimBLERemoteService *> *svcs = gClient->getServices(true);
  for (auto *svc : *svcs) {
    logLine(String("SVC ") + svc->getUUID().toString().c_str());
    std::vector<NimBLERemoteCharacteristic *> *chrs = svc->getCharacteristics(true);
    for (auto *c : *chrs) {
      String props;
      if (c->canRead()) props += "R";
      if (c->canWrite()) props += "W";
      if (c->canWriteNoResponse()) props += "w";
      if (c->canNotify()) props += "N";
      if (c->canIndicate()) props += "I";
      String line = String("  CHR ") + c->getUUID().toString().c_str() + " [" + props + "]";
      if (c->canRead()) {
        std::string v = c->readValue();
        line += " = " + hex((const uint8_t *)v.data(), v.size());
      }
      logLine(line);
      if (c->canNotify()) c->subscribe(true, onNotify);
      else if (c->canIndicate()) c->subscribe(false, onNotify);
    }
  }
}

void bleTask(void *) {
  for (;;) {
    if (gWantConnect) {
      gWantConnect = false;
      NimBLEDevice::getScan()->stop();
      if (!gClient) gClient = NimBLEDevice::createClient();
      logLine(String("connecting to ") + gTargetAddr);
      if (gClient->connect(NimBLEAddress(std::string(gTargetAddr)))) {
        logLine("connected - dumping GATT");
        dumpAndSubscribe();
      } else {
        logLine("connect failed");
      }
      NimBLEDevice::getScan()->start(0, nullptr, false);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
}  // namespace

void Bmpro::begin() {
  gMux = xSemaphoreCreateMutex();
  NimBLEDevice::init("caravan-hub");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCb(), true);
  scan->setActiveScan(true);
  scan->setInterval(160);
  scan->setWindow(48);  // low duty cycle: shares the radio with Wi-Fi
  scan->start(0, nullptr, false);
  xTaskCreatePinnedToCore(bleTask, "bmpro", 6144, nullptr, 1, nullptr, 0);
  logLine("BLE explorer started");
}

void Bmpro::connect(const char *addr) {
  strncpy(gTargetAddr, addr, 17);
  gWantConnect = true;
}

void Bmpro::disconnect() {
  if (gClient && gClient->isConnected()) gClient->disconnect();
}

bool Bmpro::connected() const { return gClient && gClient->isConnected(); }

void Bmpro::loop(cv::State &s) {
  if (gPendingValid) {
    gPendingValid = false;
    s.bat = gPending.bat;
    s.bat.source = cv::BAT_SRC_BMPRO;
  }
}

String Bmpro::log() const {
  String out;
  xSemaphoreTake(gMux, portMAX_DELAY);
  int start = (gLogHead - gLogCount + LOG_LINES) % LOG_LINES;
  for (int i = 0; i < gLogCount; i++) out += gLog[(start + i) % LOG_LINES] + "\n";
  xSemaphoreGive(gMux);
  return out;
}

String Bmpro::devices() const {
  String out = "[";
  xSemaphoreTake(gMux, portMAX_DELAY);
  for (int i = 0; i < gSeenCount; i++) {
    if (i) out += ",";
    out += String("{\"addr\":\"") + gSeen[i].addr + "\",\"name\":\"" + gSeen[i].name +
           "\",\"rssi\":" + gSeen[i].rssi + ",\"age\":" + (millis() - gSeen[i].lastMs) / 1000 + "}";
  }
  xSemaphoreGive(gMux);
  return out + "]";
}

// ---------------------------------------------------------------------------
// Protocol decoder - intentionally empty until real frames are captured.
// Workflow: enable "BMPRO explorer" in settings, connect to the CommLink, then
// change things you can see on the Trek (turn a pump on, draw a known load)
// and diff the logged notifications. Map byte offsets to fields here, e.g.:
//
//   if (strcmp(charUuid, "xxxxxxxx-....") == 0 && len >= 12) {
//     s.bat.v   = (data[2] | data[3] << 8) / 100.0f;
//     s.bat.a   = (int16_t)(data[4] | data[5] << 8) / 10.0f;
//     s.bat.soc = data[6];
//     s.bat.online = true;
//     return true;
//   }
// ---------------------------------------------------------------------------
bool Bmpro::decode(const char *charUuid, const uint8_t *data, size_t len, cv::State &s) {
  (void)charUuid;
  (void)data;
  (void)len;
  (void)s;
  return false;
}

}  // namespace hub
