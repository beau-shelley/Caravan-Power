// Caravan Power - DISPLAY firmware (Waveshare ESP32-S3-Touch-LCD-7B)
// Pure UI: all data and control goes through the hub over RS485.
#include <Arduino.h>
#include <lvgl.h>
#include <esp_task_wdt.h>

#include "app.h"
#include "board.h"
#include "canbus.h"
#include "cloud.h"
#include "evlog.h"
#include "ui/ui.h"
#ifdef DISPLAY_DEMO_MODE
#include "demo.h"
#endif

void setup() {
  Serial.setTxBufferSize(8192);  // CAN log lines queue here instead of blocking
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Caravan Power display ===");
  evlog::begin();  // onboard event log in flash (see evlog.h)
  board::begin();
#ifdef DISPLAY_DEMO_MODE
  demo::begin();
  Serial.println("Demo mode enabled (no hub link)");
#else
  app::linkBegin();
  cloud::begin();  // Wi-Fi, cloud MQTT and push alerts (settings from the hub's secrets.h)
#endif
  ui::begin();
  Serial.println("[ui] Initialization complete");
  canbus::begin();  // listen-only CAN logger, runs on core 0 (see canbus.h)

  // Watchdog: if loop() stops running for 30 s (a hang), the board restarts
  // itself. The touchscreen may be frozen then, so this is the real safety net;
  // More -> Restart system is the manual one.
  esp_task_wdt_config_t wdt = {};
  wdt.timeout_ms = 30000;
  wdt.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
  enableLoopWDT();  // the Arduino core feeds it after every loop()
}

static void sleepLogic() {
  // Wake for any new un-acknowledged warning/alarm; otherwise dim right down
  // after the idle time set on the phone (the backlight is the biggest load).
  static uint8_t lastUnacked = 0;
  uint8_t unacked = 0;
  for (int i = 0; i < app::state.alertCount; i++)
    if (!app::state.alerts[i].acked && app::state.alerts[i].sev >= cv::SEV_WARN) unacked++;
  if (unacked > lastUnacked && !board::backlightOn()) board::backlight(true);
  lastUnacked = unacked;

  int secs = ui::sleepSeconds();
  if (secs > 0 && board::backlightOn() && unacked == 0 &&
      millis() - board::lastTouchMs() > (uint32_t)secs * 1000UL) {
    board::backlight(false);
  }
  // Short timeouts (10-30 s) shouldn't throw you off the page you're reading,
  // e.g. the Level page while winding the jockey. After 5 min dimmed, go
  // back to Home so that's what you see on waking.
  static uint32_t dimmedAt = 0;
  static bool homed = false;
  if (board::backlightOn()) {
    dimmedAt = 0;
    homed = false;
  } else if (!dimmedAt) {
    dimmedAt = millis() | 1;
  } else if (!homed && millis() - dimmedAt > 5 * 60000UL) {
    ui::showPage(ui::PAGE_HOME);
    homed = true;
  }
}

void loop() {
#ifdef DISPLAY_DEMO_MODE
  demo::loop();
#else
  app::linkLoop();
#endif
  ui::loop();
  lv_timer_handler();
  sleepLogic();
  evlog::loop();
  delay(3);
}
