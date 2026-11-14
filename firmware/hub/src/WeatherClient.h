// Fetches the forecast for the caravan's location from Open-Meteo (free, no
// API key, uses BOM's ACCESS-G model among others for Australia).
// Runs on the network task: a slow fetch never stalls the sensors/display.
#pragma once
#include <Arduino.h>
#include "CaravanModel.h"

namespace hub {

class WeatherClient {
 public:
  void loop(cv::State &s);       // call from the network task
  void refreshSoon() { force_ = true; }

 private:
  uint32_t nextMs_ = 0;
  float lastLat_ = 0, lastLon_ = 0;
  bool force_ = true;
};

extern WeatherClient gWeather;

}  // namespace hub
