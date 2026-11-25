// Onboard event log: a few lines per event (boots and why, Wi-Fi, cloud,
// hub link, push alerts, a heartbeat every 15 min) appended to the display's
// own flash (LittleFS on the spare "spiffs" partition), so it survives power
// cuts and restarts. Read it back over USB with the serial command "log"
// (or "log clear"); the latest lines also show on Settings.
#pragma once
#include <Arduino.h>

namespace evlog {

void begin();                                         // mount + log the boot
void add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));  // also echoed to Serial
void loop();                                          // heartbeat + serial commands (UI task)
// Newest lines, newest last, at most maxLines, into buf.
void tail(char *buf, size_t len, int maxLines);

}  // namespace evlog
