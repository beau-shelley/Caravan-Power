// Demo data mode for the hub (build env hub-esp32-demo / hub-demo).
// Replaces the sensor readings with made-up, slowly changing values so the
// display, web dashboard and phone app can be tried on the bench with no
// caravan wiring. Everything after the sensors is real: the alert engine,
// the RS485 link to the display, pump relays and the dashboard all run as
// normal on the fake numbers.
#pragma once

#include "CaravanModel.h"
#include "Heater.h"

namespace hub {
namespace demo {

#ifdef HUB_DEMO_MODE
constexpr bool kOn = true;
#else
constexpr bool kOn = false;
#endif

void begin(cv::State &s);
// Call every loop after the network has stamped the clock; it rewrites the
// sensor parts of the state once a second.
void tick(cv::State &s, uint32_t nowMs);
// Fake 12 V load current, standing in for the BMPRO's load reading so the
// mains-charger inference has a measured load to work with.
float loadA();
// Pretend Bluetooth diesel heater: answers commands like the real one would
// and reports status, so the heater card can be tried on the bench.
HeaterDriver &heater();

}  // namespace demo
}  // namespace hub
