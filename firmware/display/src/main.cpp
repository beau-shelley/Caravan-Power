// Caravan Power - DISPLAY firmware (Waveshare ESP32-S3-Touch-LCD-7B)
// Pure UI: all data and control goes through the hub over RS485.
#include <Arduino.h>
#include <lvgl.h>

#include "app.h"
#include "board.h"
#include "ui/ui.h"
#ifdef DISPLAY_DEMO_MODE
#include "demo.h"
#endif

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Caravan Power display ===");
  board::begin();
#ifdef DISPLAY_DEMO_MODE
  demo::begin();
  Serial.println("Demo mode enabled (no hub link)");
#else
  app::linkBegin();
#endif
  ui::begin();
  Serial.println("[ui] Initialization complete");
}

static void sleepLogic() {
  // Wake for any new un-acknowledged warning/alarm; otherwise sleep after the
  // configured idle time (saves ~1 W - the backlight is the biggest load).
  static uint8_t lastUnacked = 0;
  uint8_t unacked = 0;
  for (int i = 0; i < app::state.alertCount; i++)
    if (!app::state.alerts[i].acked && app::state.alerts[i].sev >= cv::SEV_WARN) unacked++;
  if (unacked > lastUnacked && !board::backlightOn()) board::backlight(true);
  lastUnacked = unacked;

  int mins = ui::sleepMinutes();
  if (mins > 0 && board::backlightOn() && unacked == 0 &&
      millis() - board::lastTouchMs() > (uint32_t)mins * 60000UL) {
    board::backlight(false);
    ui::showPage(ui::PAGE_HOME);  // wake up on the home screen
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
  delay(3);
}
