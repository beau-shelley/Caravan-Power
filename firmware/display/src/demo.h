#pragma once

#include <ArduinoJson.h>

namespace demo {
void begin();
void loop();
// Demo builds have no hub, so commands from the UI are applied here instead.
void command(const JsonDocument &cmd);
}  // namespace demo
