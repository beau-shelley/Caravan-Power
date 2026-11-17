// BMPRO OdysseyLink connection protocol (see docs/BMPRO_PROTOCOL.md), decoded
// from an Android HCI log of the BMPRO app on 2026-11-17.
// Messages on the "SETEC CAN BLE" characteristic: [op] 00 [node] [param] [len] [data...]
// Portable (no Arduino deps) so it is unit tested on the host.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cv {
namespace bmpro {

constexpr const char *AUTH_UUID = "ffffffff-ffff-5345-5445-432041555448";       // "SETEC AUTH"
constexpr const char *CAN_UUID = "ffffff53-4554-4543-2043-414e20424c45";        // "SETEC CAN BLE"
constexpr const char *INIT_UUID = "5e132001-73a9-4a6a-ae5f-c7bff0bd075e";       // app writes 00..13 here at start

// Login seen in the log: the OdysseyLink sends this challenge, the app answers.
constexpr uint8_t AUTH_CHALLENGE[4] = {0xe1, 0xc0, 0x7b, 0x00};
constexpr uint8_t AUTH_ANSWER[8] = {0xa2, 0xf1, 0x4b, 0x78, 0xef, 0xbf, 0xbd, 0xef};

enum : uint8_t { OP_INFO = 0x14, OP_WRITE_REPLY = 0x16, OP_VALUE = 0x18, OP_READ = 0x3c, OP_WRITE = 0x3e };
enum : uint8_t { NODE_POWER = 0x40, NODE_SENSORS = 0x90 };
enum : uint8_t {
  P_PUMP1 = 0x4c,
  P_PUMP2 = 0x4d,
  P_BAT_MA = 0x61,
  P_BAT_MV = 0x62,
  P_SOLAR_MA = 0x64,
  P_SOC = 0x66,
  P_MIN_LEFT = 0x67,
  P_TANKS = 0x80,  // node 0x90
};

struct Msg {
  uint8_t op = 0, node = 0, param = 0, len = 0;
  const uint8_t *data = nullptr;
};

bool parse(const uint8_t *d, size_t n, Msg &m);

// What a message tells us; only fields with has* set are valid.
struct Values {
  bool hasBatV = false, hasBatA = false, hasSolarA = false, hasSoc = false, hasMinLeft = false;
  float batV = 0, batA = 0, solarA = 0, soc = 0;
  int32_t minLeft = -1;
  bool hasTanks = false;
  float tankPct[4] = {};  // Top, Bottom, Grey, 4th (quarters -> %)
  bool hasPump[2] = {};
  bool pumpOn[2] = {};
};

// Folds one CAN message (value or write reply) into v. False if nothing known.
bool apply(const Msg &m, Values &v);

// Bytes for "switch pump i (0/1) on/off", like the app. Returns the length (6).
size_t encodePump(int pump, bool on, uint8_t *buf);

}  // namespace bmpro
}  // namespace cv
