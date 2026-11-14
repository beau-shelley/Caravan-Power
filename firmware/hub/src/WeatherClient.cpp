#include "WeatherClient.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "Comms.h"
#include "HubConfig.h"
#include "root_ca_web.h"

namespace hub {

WeatherClient gWeather;

void WeatherClient::search(const char *q) {
  StateLock l;
  snprintf(query_, sizeof(query_), "%s", q ? q : "");
}

void WeatherClient::runSearch() {
  char q[48];
  {
    StateLock l;
    memcpy(q, query_, sizeof(q));
    query_[0] = 0;
  }
  JsonDocument out;
  out["t"] = "loc_results";
  JsonArray list = out["list"].to<JsonArray>();
  char path[200];
  cv::geocodePath(path, sizeof(path), q);
  WiFiClientSecure tls;
  tls.setCACert(WEB_ROOTS_PEM);
  HTTPClient http;
  http.setTimeout(15000);
  int code = -1;
  if (WiFi.status() == WL_CONNECTED && http.begin(tls, String("https://geocoding-api.open-meteo.com") + path)) {
    code = http.GET();
    if (code == 200) {
      JsonDocument in;
      if (!deserializeJson(in, http.getString())) {
        for (JsonObjectConst r : in["results"].as<JsonArrayConst>()) {
          JsonObject o = list.add<JsonObject>();
          char name[24];
          snprintf(name, sizeof(name), "%s %s", r["name"] | "", cv::auStateAbbrev(r["admin1"] | ""));
          o["name"] = name;
          o["area"] = r["admin2"] | "";  // council area, helps tell duplicates apart
          o["lat"] = r["latitude"] | 0.0f;
          o["lon"] = r["longitude"] | 0.0f;
        }
      }
    }
    http.end();
  }
  if (code != 200) out["err"] = WiFi.status() == WL_CONNECTED ? "Search failed - try again" : "No internet connection";
  String s;
  serializeJson(out, s);
  StateLock l;
  gLink.queueReply(s);
}

void WeatherClient::loop(cv::State &s) {
  if (query_[0]) runSearch();
  if (WiFi.status() != WL_CONNECTED || time(nullptr) < 1700000000) return;

  float lat, lon;
  uint8_t src;
  char place[24];
  {
    StateLock l;
    const LocationCfg &loc = gConfig.location;
    if (loc.src == cv::LOC_NONE) return;  // no location yet
    lat = loc.lat;
    lon = loc.lon;
    src = loc.src;
    memcpy(place, loc.name, sizeof(place));
  }
  // Refetch every 30 min, or straight away after moving > 5 km.
  bool moved = cv::distanceKm(lat, lon, lastLat_, lastLon_) > 5;
  if (!force_ && !moved && (int32_t)(millis() - nextMs_) < 0) return;
  force_ = false;
  nextMs_ = millis() + 5UL * 60 * 1000;  // retry in 5 min if this fails

  char path[400];
  cv::openMeteoPath(path, sizeof(path), lat, lon);
  WiFiClientSecure tls;
  tls.setCACert(WEB_ROOTS_PEM);
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(tls, String("https://api.open-meteo.com") + path)) return;
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[weather] HTTP %d\n", code);
    http.end();
    return;
  }
  String body = http.getString();
  http.end();

  cv::Weather w;
  if (!cv::parseOpenMeteo(body.c_str(), body.length(), w)) {
    Serial.println("[weather] could not parse response");
    return;
  }
  w.updated = (uint32_t)time(nullptr);
  w.locSrc = src;
  memcpy(w.place, place, sizeof(w.place));
  {
    StateLock l;
    s.wx = w;
  }
  lastLat_ = lat;
  lastLon_ = lon;
  nextMs_ = millis() + 30UL * 60 * 1000;
  Serial.printf("[weather] %.1f C, %s, gusts today %.0f km/h\n", w.temp, cv::wxText(w.code), w.day[0].gustMax);
}

}  // namespace hub
