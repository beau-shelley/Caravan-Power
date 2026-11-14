#include "Weather.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cv {

void openMeteoPath(char *buf, size_t n, float lat, float lon) {
  snprintf(buf, n,
           "/v1/forecast?latitude=%.3f&longitude=%.3f"
           "&current=temperature_2m,weather_code,wind_speed_10m,wind_gusts_10m,is_day"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
           "precipitation_sum,wind_gusts_10m_max&timezone=auto&forecast_days=%d",
           lat, lon, WX_DAYS);
}

// Day of week for an ISO date "YYYY-MM-DD" (Sakamoto's algorithm).
static int8_t weekday(const char *iso) {
  if (!iso || strlen(iso) < 10) return -1;
  int y = atoi(iso), m = atoi(iso + 5), d = atoi(iso + 8);
  if (m < 1 || m > 12) return -1;
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (int8_t)((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7);
}

bool parseOpenMeteo(const char *json, size_t len, Weather &w) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  JsonObjectConst cur = doc["current"], daily = doc["daily"];
  if (cur.isNull() || daily.isNull() || !cur["temperature_2m"].is<float>()) return false;
  w.temp = cur["temperature_2m"];
  w.code = cur["weather_code"] | 0;
  w.wind = cur["wind_speed_10m"] | 0.0f;
  w.gust = cur["wind_gusts_10m"] | 0.0f;
  w.isDay = (cur["is_day"] | 1) != 0;
  for (int i = 0; i < WX_DAYS; i++) {
    DailyWx &d = w.day[i];
    d.wday = weekday(daily["time"][i] | "");
    d.code = daily["weather_code"][i] | 0;
    d.tMax = daily["temperature_2m_max"][i] | 0.0f;
    d.tMin = daily["temperature_2m_min"][i] | 0.0f;
    d.rainProb = daily["precipitation_probability_max"][i] | 0;
    d.rainMm = daily["precipitation_sum"][i] | 0.0f;
    d.gustMax = daily["wind_gusts_10m_max"][i] | 0.0f;
  }
  w.valid = true;
  return true;
}

const char *wxText(uint8_t c) {
  switch (c) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Storm, hail";
    default: return "Unknown";
  }
}

uint16_t wxIcon(uint8_t c, bool day) {
  if (c == 0) return day ? 0xF00D : 0xF02E;           // sunny / night clear
  if (c == 1 || c == 2) return day ? 0xF002 : 0xF086; // partly cloudy
  if (c == 3) return 0xF013;                          // cloudy
  if (c == 45 || c == 48) return 0xF014;              // fog
  if (c >= 51 && c <= 57) return 0xF01C;              // drizzle
  if (c >= 61 && c <= 67) return 0xF019;              // rain
  if (c >= 71 && c <= 77) return 0xF01B;              // snow
  if (c >= 80 && c <= 82) return 0xF01A;              // showers
  if (c == 85 || c == 86) return 0xF01B;
  if (c >= 95) return 0xF01E;                         // thunderstorm
  return 0xF013;
}

const char *wxEmoji(uint8_t c, bool day) {
  if (c == 0) return day ? "☀️" : "\U0001F319";
  if (c == 1 || c == 2) return "⛅";
  if (c == 3) return "☁️";
  if (c == 45 || c == 48) return "\U0001F32B️";
  if (c >= 51 && c <= 67) return "\U0001F327️";
  if (c >= 71 && c <= 77) return "❄️";
  if (c >= 80 && c <= 82) return "\U0001F326️";
  if (c >= 95) return "⛈️";
  return "☁️";
}

static float r1(float v) { return std::round(v * 10.0f) / 10.0f; }

void weatherToJson(const Weather &w, JsonObject o) {
  o["ok"] = w.valid;
  if (!w.valid) return;
  o["up"] = w.updated;
  o["src"] = w.locSrc;
  o["pl"] = w.place;
  o["t"] = r1(w.temp);
  o["c"] = w.code;
  o["day"] = w.isDay;
  o["w"] = r1(w.wind);
  o["g"] = r1(w.gust);
  // Compact: [code, weekday, max, min, rain %, rain mm, gust]
  JsonArray days = o["d"].to<JsonArray>();
  for (const DailyWx &d : w.day) {
    JsonArray a = days.add<JsonArray>();
    a.add(d.code);
    a.add(d.wday);
    a.add(r1(d.tMax));
    a.add(r1(d.tMin));
    a.add(d.rainProb);
    a.add(r1(d.rainMm));
    a.add(r1(d.gustMax));
  }
}

void weatherFromJson(Weather &w, JsonObjectConst o) {
  w.valid = o["ok"] | false;
  if (!w.valid) return;
  w.updated = o["up"] | 0u;
  w.locSrc = o["src"] | 0;
  const char *pl = o["pl"] | "";
  strncpy(w.place, pl, sizeof(w.place) - 1);
  w.place[sizeof(w.place) - 1] = 0;
  w.temp = o["t"] | 0.0f;
  w.code = o["c"] | 0;
  w.isDay = o["day"] | true;
  w.wind = o["w"] | 0.0f;
  w.gust = o["g"] | 0.0f;
  for (int i = 0; i < WX_DAYS; i++) {
    JsonArrayConst a = o["d"][i];
    DailyWx &d = w.day[i];
    d.code = a[0] | 0;
    d.wday = a[1] | -1;
    d.tMax = a[2] | 0.0f;
    d.tMin = a[3] | 0.0f;
    d.rainProb = a[4] | 0;
    d.rainMm = a[5] | 0.0f;
    d.gustMax = a[6] | 0.0f;
  }
}

const char *auStateAbbrev(const char *a) {
  static const char *map[][2] = {{"New South Wales", "NSW"}, {"Victoria", "VIC"}, {"Queensland", "QLD"},
                                 {"South Australia", "SA"}, {"Western Australia", "WA"}, {"Tasmania", "TAS"},
                                 {"Northern Territory", "NT"}, {"Australian Capital Territory", "ACT"}};
  if (!a) return "";
  for (auto &m : map)
    if (!strcmp(a, m[0])) return m[1];
  return a;
}

void geocodePath(char *buf, size_t n, const char *q) {
  int k = snprintf(buf, n, "/v1/search?count=6&language=en&countryCode=AU&name=");
  static const char hex[] = "0123456789ABCDEF";
  for (const unsigned char *p = (const unsigned char *)q; *p && k + 4 < (int)n; p++) {
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '.') {
      buf[k++] = (char)*p;
    } else {  // percent-encode everything else (spaces, apostrophes, UTF-8)
      buf[k++] = '%';
      buf[k++] = hex[*p >> 4];
      buf[k++] = hex[*p & 15];
    }
  }
  buf[k] = 0;
}

// ---------------------------------------------------------------- NMEA
static int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool NmeaParser::feed(uint8_t b) {
  if (b == '$') {
    len_ = 0;
    buf_[len_++] = '$';
    return false;
  }
  if (len_ == 0) return false;
  if (b == '\r' || b == '\n') {
    buf_[len_] = 0;
    bool ok = parse();
    len_ = 0;
    return ok;
  }
  if (len_ < sizeof(buf_) - 1) buf_[len_++] = (char)b;
  else len_ = 0;  // overlong: drop
  return false;
}

// "3236.1234" + "S" -> -32.60206
static bool coord(const char *v, const char *hemi, int degDigits, double &out) {
  if (!v[0] || !hemi[0] || (int)strlen(v) < degDigits + 2) return false;
  char d[4] = {0};
  memcpy(d, v, degDigits);
  double deg = atof(d) + atof(v + degDigits) / 60.0;
  if (hemi[0] == 'S' || hemi[0] == 'W') deg = -deg;
  out = deg;
  return true;
}

bool NmeaParser::parse() {
  // Checksum: XOR of everything between '$' and '*'
  char *star = strchr(buf_, '*');
  if (!star || star - buf_ < 7) return false;
  int hi = hexv(star[1]), lo = star[1] ? hexv(star[2]) : -1;
  if (hi < 0 || lo < 0) return false;
  uint8_t x = 0;
  for (char *p = buf_ + 1; p < star; p++) x ^= (uint8_t)*p;
  if (x != (uint8_t)((hi << 4) | lo)) return false;
  *star = 0;
  if (strncmp(buf_ + 3, "RMC", 3) != 0) return false;

  // Split on commas (keeping empty fields).
  const char *f[13] = {};
  int n = 0;
  char *p = buf_;
  f[n++] = p;
  while (*p && n < 13) {
    if (*p == ',') {
      *p = 0;
      f[n++] = p + 1;
    }
    p++;
  }
  if (n < 7) return false;
  // RMC: 0 id, 1 time, 2 status A/V, 3 lat, 4 N/S, 5 lon, 6 E/W
  if (f[2][0] != 'A') {
    fix_ = false;
    return false;
  }
  double la, lo2;
  if (!coord(f[3], f[4], 2, la) || !coord(f[5], f[6], 3, lo2)) return false;
  lat_ = la;
  lon_ = lo2;
  fix_ = true;
  return true;
}

float distanceKm(float lat1, float lon1, float lat2, float lon2) {
  const float r = 6371.0f, d2r = 3.14159265f / 180.0f;
  float dLat = (lat2 - lat1) * d2r, dLon = (lon2 - lon1) * d2r;
  float a = std::sin(dLat / 2) * std::sin(dLat / 2) +
            std::cos(lat1 * d2r) * std::cos(lat2 * d2r) * std::sin(dLon / 2) * std::sin(dLon / 2);
  return 2 * r * std::asin(std::sqrt(a));
}

}  // namespace cv
