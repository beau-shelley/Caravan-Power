// Display-side application model: latest data from the hub + command sending.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "CaravanModel.h"

namespace app {

extern cv::State state;
extern cv::Meta meta;
extern cv::RuleSet rules;
extern bool linkUp;
extern bool stateDirty;
extern uint32_t metaVer, rulesVer;  // bumped whenever names / rules arrive
extern uint32_t linkErrors;

void linkBegin();
void linkLoop();

// Queue a command for the hub, e.g. {"c":"pump","i":0,"on":true}.
void send(JsonDocument &cmd);
// Convenience wrappers
void cmd(const char *c);
void cmdPump(int i, bool on);
void cmdInverter(bool on);
void cmdAircon(int preset);  // -1 = off
// Diesel heater: {"c":"heater", key: value}, e.g. ("temp", 22), ("level", 6).
void cmdHeaterOn(bool on);
void cmdHeaterSet(const char *key, int value);
void cmdHeaterMode(bool level);
// Secure mode (anti-theft): {"c":"secure","arm":true|false}
void cmdSecure(bool arm);

// Hook for replies the UI cares about (errors, learn status, ...)
using ReplyHandler = void (*)(JsonObjectConst msg);
void onReply(ReplyHandler h);

}  // namespace app
