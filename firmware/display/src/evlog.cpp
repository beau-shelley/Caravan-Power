#include "evlog.h"

#include <FS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

#include "app.h"
#include "canbus.h"
#include "cloud.h"

namespace evlog {

static const char *kLog = "/events.log", *kOld = "/events.1.log";
static const size_t kMaxBytes = 256 * 1024;  // then rotate: ~1 year of normal use
static SemaphoreHandle_t gMux = nullptr;
static bool gOk = false;
static uint32_t gBoot = 0;

// Last lines kept in RAM for the Settings page (no flash reads on the UI path).
static const int kRam = 8;
static char gRam[kRam][112];
static int gRamNext = 0, gRamCount = 0;

static const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "CRASH (panic)";
    case ESP_RST_INT_WDT: return "CRASH (interrupt watchdog)";
    case ESP_RST_TASK_WDT: return "HANG (task watchdog)";
    case ESP_RST_WDT: return "HANG (watchdog)";
    case ESP_RST_BROWNOUT: return "BROWNOUT (power dipped)";
    case ESP_RST_EXT: return "reset pin";
    case ESP_RST_USB: return "USB reset";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default: return "unknown";
  }
}

static void stamp(char *b, size_t n) {
  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm lt;
    localtime_r(&now, &lt);
    strftime(b, n, "%Y-%m-%d %H:%M:%S", &lt);
  } else {
    uint32_t s = millis() / 1000;  // clock not set yet (no Wi-Fi): time since boot
    snprintf(b, n, "boot+%02u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
  }
}

static void write(const char *line) {
  if (!gOk) return;
  File f = LittleFS.open(kLog, FILE_APPEND);
  if (!f) return;
  f.println(line);
  size_t size = f.size();
  f.close();
  if (size > kMaxBytes) {
    LittleFS.remove(kOld);
    LittleFS.rename(kLog, kOld);
  }
}

void add(const char *fmt, ...) {
  char msg[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  char ts[24];
  stamp(ts, sizeof(ts));
  char line[128];
  snprintf(line, sizeof(line), "%s #%u %s", ts, (unsigned)gBoot, msg);
  Serial.printf("[log] %s\n", line);
  if (!gMux) return;
  if (xSemaphoreTake(gMux, pdMS_TO_TICKS(500)) != pdTRUE) return;
  write(line);
  snprintf(gRam[gRamNext], sizeof(gRam[0]), "%s  %s", ts, msg);
  gRamNext = (gRamNext + 1) % kRam;
  if (gRamCount < kRam) gRamCount++;
  xSemaphoreGive(gMux);
}

void begin() {
  gMux = xSemaphoreCreateMutex();
  // The spare 1.5 MB "spiffs" partition; formatted the first time.
  gOk = LittleFS.begin(true, "/littlefs", 4, "spiffs");
  if (!gOk) Serial.println("[log] ERROR: can't mount the log partition");
  // Boot counter, so lines from one run group together.
  if (gOk) {
    File f = LittleFS.open("/boots", FILE_READ);
    if (f) {
      gBoot = f.parseInt();
      f.close();
    }
    gBoot++;
    f = LittleFS.open("/boots", FILE_WRITE);
    if (f) {
      f.print(gBoot);
      f.close();
    }
  }
  add("BOOT: display started, reason: %s, fw %s %s", resetReason(), __DATE__, __TIME__);
}

void tail(char *buf, size_t len, int maxLines) {
  if (!len) return;
  buf[0] = 0;
  if (!gMux || xSemaphoreTake(gMux, pdMS_TO_TICKS(50)) != pdTRUE) return;
  int n = min(maxLines, gRamCount);
  size_t k = 0;
  for (int i = n; i > 0 && k + 1 < len; i--) {
    const char *l = gRam[(gRamNext - i + kRam) % kRam];
    int w = snprintf(buf + k, len - k, "%s%s", k ? "\n" : "", l);
    if (w < 0) break;
    k += w;
  }
  xSemaphoreGive(gMux);
}

static void dump() {
  if (!gOk) {
    Serial.println("[log] not mounted");
    return;
  }
  xSemaphoreTake(gMux, portMAX_DELAY);
  Serial.println("----- event log (older file first) -----");
  for (const char *p : {kOld, kLog}) {
    File f = LittleFS.open(p, FILE_READ);
    if (!f) continue;
    while (f.available()) {
      char b[256];
      size_t r = f.read((uint8_t *)b, sizeof(b));
      Serial.write((const uint8_t *)b, r);
      Serial.flush();
    }
    f.close();
  }
  Serial.println("----- end of event log -----");
  xSemaphoreGive(gMux);
}

void loop() {
  // Heartbeat every 15 min: the last one before a gap shows when power went.
  static uint32_t lastHb = 0;
  if (!lastHb || millis() - lastHb >= 15 * 60000UL) {
    if (lastHb) {
      add("hb: up %uh%02um, Wi-Fi %s %ddBm, cloud %s, hub %s, heap %u/%u KB",
          (unsigned)(millis() / 3600000), (unsigned)(millis() / 60000 % 60),
          cloud::wifiUp() ? WiFi.SSID().c_str() : "down", cloud::wifiUp() ? (int)WiFi.RSSI() : 0,
          cloud::connected() ? "up" : "down", app::linkUp ? "ok" : "DOWN",
          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    }
    lastHb = millis() | 1;
  }
  static bool clockSet = false;
  if (!clockSet && time(nullptr) > 1700000000) {
    clockSet = true;
    add("clock set from the internet (up %us)", (unsigned)(millis() / 1000));
  }
  // Serial commands: "log" prints the whole log, "log clear" empties it.
  static char cmd[24];
  static size_t n = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r') continue;
    if (ch != '\n') {
      if (n + 1 < sizeof(cmd)) cmd[n++] = ch;
      continue;
    }
    cmd[n] = 0;
    n = 0;
    if (cmd[0] && !cmd[1]) canbus::serialCommand(cmd[0]);  // CAN logger letters
    else if (!strcmp(cmd, "log")) dump();
    else if (!strcmp(cmd, "log clear") && gOk) {
      xSemaphoreTake(gMux, portMAX_DELAY);
      LittleFS.remove(kLog);
      LittleFS.remove(kOld);
      xSemaphoreGive(gMux);
      add("log cleared");
    }
  }
}

}  // namespace evlog
