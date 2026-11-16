// Cloud link on the display board: the classic ESP32 hub hasn't the RAM for
// TLS next to Wi-Fi + Bluetooth, so the display (ESP32-S3, 8 MB PSRAM) joins
// Wi-Fi itself and
//   - publishes the hub's state / meta / rules to the cloud MQTT broker (EMQX),
//     on the same topics the hub used, so the phone app and web page work as-is;
//   - relays commands from caravan/<id>/cmd to the hub over RS485 and the
//     hub's replies back on caravan/<id>/resp;
//   - sends the hub's push alerts to ntfy.
// Settings come from the hub's secrets.h (same Wi-Fi, broker and ntfy topic).
// Everything network runs in its own task on core 0, so a slow TLS handshake
// over Starlink never stalls the touchscreen.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace cloud {

void begin();
bool wifiUp();
bool connected();  // MQTT broker connected

// From the RS485 link (UI task). All cheap: they copy and return.
void onHubState(JsonDocument &state);              // every state frame (~1 s)
void onHubMeta(JsonVariantConst meta);             // hello / meta frames
void onHubRules(JsonVariantConst rules);           // hello / rules frames
void onHubReply(const char *json, size_t len);     // a frame carrying "rid"
void onHubEvent(const char *json, size_t len);     // {"t":"event",...}
void onHubPush(JsonObjectConst push);              // {"t":"push",...}
void onHubLink(bool up);                           // RS485 link up / down

// Commands that came in from the broker, for the link to send to the hub.
// Returns false when there are none. Call from the UI task.
bool takeCommand(JsonDocument &cmd);

}  // namespace cloud
