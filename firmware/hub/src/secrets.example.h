// Copy to secrets.h and fill in. secrets.h is git-ignored.
#pragma once

// Starlink (or any) Wi-Fi
#define WIFI_SSID "Starlink"
#define WIFI_PASS "changeme"
// Optional second network (e.g. phone hotspot), tried in turn with the first
// #define WIFI_SSID2 "AndroidAP"
// #define WIFI_PASS2 "changeme"

// Device id used in MQTT topics and the mDNS name (caravan.local)
#define DEVICE_ID "caravan"
#define HOSTNAME "caravan"

// Local web UI login (http://caravan.local). Leave user empty to disable auth.
#define WEB_USER "admin"
#define WEB_PASS "changeme"

// Cloud MQTT broker (EMQX Cloud Serverless, free). TLS on 8883.
// Address = the deployment's "Address" on its Overview page. Leave the
// xxxxxxxx placeholder to run without the cloud.
// Starlink uses CGNAT, so the hub connects OUT to the broker; nothing is
// exposed to the internet and no port forwarding is needed.
#define MQTT_HOST "xxxxxxxx.ala.asia-southeast1.emqxsl.com"
#define MQTT_PORT 8883
#define MQTT_USER "caravan-hub"
#define MQTT_PASS "changeme"

// The display board (ESP32-S3) runs the cloud MQTT link and the ntfy push
// alerts, using the settings in this file: the classic ESP32 hub hasn't the RAM
// for TLS next to Wi-Fi + Bluetooth. Both boards build with this one file.
// Set to 0 only for a hub with no display (the hub then does it itself).
#define CLOUD_ON_DISPLAY 1

// Phone push notifications via ntfy (https://ntfy.sh, free iOS/Android app).
// Pick a long random topic name - anyone who knows it can read your alerts.
#define NTFY_SERVER "https://ntfy.sh"
#define NTFY_TOPIC "caravan-alerts-CHANGE-ME-8f3k2"
#define NTFY_TOKEN ""   // optional access token if you use a protected topic

// POSIX timezone (default: AEST/AEDT for NSW/VIC/TAS/ACT)
#define TZ_INFO "AEST-10AEDT,M10.1.0,M4.1.0/3"
