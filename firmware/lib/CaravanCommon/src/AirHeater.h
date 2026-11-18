// Diesel heater Bluetooth protocol, as used by the "AirHeaterBLE" phone app
// that ships with many Chinese 2-5 kW air heaters (Vevor, Hcalory, and very
// likely the Power Science 2 kW in Beau's van).
//
// CONFIRMED on Beau's Power Science heater (69:96:1c:04:0b:f0, public
// address) from an Android HCI snoop of the AirHeaterBLE app, 2026-11-18:
// passkey 1234, status poll every 3 s, on/off, mode, set temperature and the
// cabin temperature (26 C, matching the app) all match the layout below.
// The app also sends cmd 10 at connect with the phone's clock as minutes
// since midnight (0x04C0 = 20:16); the hub doesn't need it. Byte 5 (step)
// stayed 0 during a short 25 s run, so the start-up phases are not yet seen.
// The layout follows bderleta/vevor-ble-bridge. See docs/HEATER.md.
//
// Transport: one GATT service 0xFFE0 with one characteristic 0xFFE1 that is
// written to (commands) and notifies (status).
//
// Command, 8 bytes:
//   AA 55 <pass/100> <pass%100> <cmd> <arg lo> <arg hi> <sum of bytes 2..6>
//   pass = the 4-digit passkey set in the app (factory default 1234 -> 0C 22)
//   cmd 1 = request status (arg 0)
//   cmd 2 = mode: arg 1 = fixed power level, 2 = thermostat (set temperature)
//   cmd 3 = on/off: arg 1 = start, 0 = stop
//   cmd 4 = set point: power level 1-10, or temperature 8-36 C (by mode)
//
// Status notification, AA 55 then (byte offsets from the AA):
//    3 running (0 = off, 1 = on)      4 error code (0 = none, 1..10 = E-01..E-10)
//    5 step: 0 standby, 1 self test, 2 ignition, 3 running, 4 cooling down
//  6,7 altitude (not used)            8 mode: 1 = power level, 2 = thermostat
//    9 set point (level in level mode, temperature in thermostat mode)
//   10 power level - 1 (thermostat mode)
// 11,12 supply voltage, 0.1 V, little endian
// 13,14 heat exchanger (case) temperature, C, signed little endian
// 15,16 cabin temperature, C, signed little endian
// Some newer heaters send an encrypted "AA 66" frame instead. Those are kept
// in the log but not decoded (decode() returns false) until one is captured.
#pragma once
#include <cstddef>
#include <cstdint>

#include "CaravanModel.h"

namespace cv {
namespace airheater {

constexpr const char *SERVICE_UUID = "ffe0";
constexpr const char *CHAR_UUID = "ffe1";
constexpr uint16_t DEFAULT_PASSKEY = 1234;
constexpr size_t CMD_LEN = 8;
constexpr size_t STATUS_MIN_LEN = 17;

enum Cmd : uint8_t { CMD_STATUS = 1, CMD_MODE = 2, CMD_POWER = 3, CMD_SETPOINT = 4 };
enum WireMode : uint8_t { WIRE_LEVEL = 1, WIRE_TEMP = 2 };

// Builds one command frame into out[CMD_LEN].
void encode(uint8_t cmd, uint16_t arg, uint16_t passkey, uint8_t out[CMD_LEN]);

// Turns a client command into the frames to write, in order (mode first, so
// a set point lands in the right mode). Returns how many frames were written
// to out (at most 4). `cur` is the heater's current state.
int framesFor(const HeaterCmd &c, const Heater &cur, uint16_t passkey, uint8_t out[][CMD_LEN], int maxFrames);

// Decodes a status notification into h (online/link untouched). Returns
// false for anything that isn't a plain AA 55 status frame.
bool decode(const uint8_t *data, size_t len, Heater &h);

}  // namespace airheater

// ---------------------------------------------------------------- simulator
// A pretend heater for the hub's fake-data build and the display demo build.
// Real heaters take ~3 minutes to light and ~3 to cool down; this one is sped
// up (40 s / 30 s) so it can be watched on the bench.
class HeaterSim {
 public:
  // Applies a command the way the real heater would. Returns false (with
  // *why) if it would refuse, e.g. starting while still cooling down.
  bool apply(Heater &h, const HeaterCmd &c, const char **why);
  // Advances dt seconds. ambientC = the van's inside temperature without the
  // heater; batV is reported as the heater's supply voltage.
  void tick(Heater &h, float dt, float ambientC, float batV);
  // 12 V current the heater would draw right now (glow plug while lighting).
  float currentA(const Heater &h) const;
  float cabinC() const { return cabin_; }

 private:
  float phaseSec_ = 0;
  float cabin_ = -100;  // -100 = not started yet (take ambient)
};

}  // namespace cv
