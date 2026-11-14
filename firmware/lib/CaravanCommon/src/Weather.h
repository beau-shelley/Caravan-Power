// Weather forecast (Open-Meteo, free, no API key) + GPS position (NMEA).
// Portable (no Arduino deps) so the parsers are unit tested on the host.
#pragma once
#include <cstddef>
#include <cstdint>
#include <ArduinoJson.h>

namespace cv {

constexpr int WX_DAYS = 4;

struct DailyWx {
  uint8_t code = 0;       // WMO weather code
  int8_t wday = -1;       // 0 = Sunday
  float tMax = 0, tMin = 0;
  uint8_t rainProb = 0;   // %
  float rainMm = 0;
  float gustMax = 0;      // km/h
};

enum LocationSource : uint8_t { LOC_NONE = 0, LOC_GPS = 1, LOC_PHONE = 2, LOC_MANUAL = 3 };

struct Weather {
  bool valid = false;
  uint32_t updated = 0;   // unix time of the fetch
  uint8_t locSrc = LOC_NONE;
  char place[24] = {0};
  float temp = 0, wind = 0, gust = 0;
  uint8_t code = 0;
  bool isDay = true;
  DailyWx day[WX_DAYS];
};

// Open-Meteo request path (append to https://api.open-meteo.com).
void openMeteoPath(char *buf, size_t n, float lat, float lon);
// Parses the Open-Meteo JSON into w (keeps place/locSrc). False if malformed.
bool parseOpenMeteo(const char *json, size_t len, Weather &w);

const char *wxText(uint8_t code);                 // "Partly cloudy"
uint16_t wxIcon(uint8_t code, bool isDay);        // Weather Icons codepoint (U+F0xx)
const char *wxEmoji(uint8_t code, bool isDay);    // for the web dashboard

void weatherToJson(const Weather &w, JsonObject o);
void weatherFromJson(Weather &w, JsonObjectConst o);

// ---------------------------------------------------------------- GPS
// Minimal NMEA 0183 reader: $xxRMC sentences (any talker: GP, GN, GL...).
class NmeaParser {
 public:
  // Feed one byte; true when a valid fix has just been decoded.
  bool feed(uint8_t b);
  double lat() const { return lat_; }
  double lon() const { return lon_; }
  bool hasFix() const { return fix_; }

 private:
  char buf_[96];
  size_t len_ = 0;
  double lat_ = 0, lon_ = 0;
  bool fix_ = false;
  bool parse();
};

// Great-circle distance in km.
float distanceKm(float lat1, float lon1, float lat2, float lon2);

}  // namespace cv
