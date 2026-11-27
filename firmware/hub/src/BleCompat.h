// Small shims so the hub's Bluetooth code builds with both NimBLE-Arduino 1.4
// (classic ESP32 boards, Arduino 2) and NimBLE-Arduino 2.x (the S3 hub on
// Arduino 3 / ESP-IDF 5). Include after <NimBLEDevice.h>.
#pragma once
#include <NimBLEDevice.h>
#include <string>
#include <vector>

#if defined(NIMBLE_CPP_VERSION_MAJOR) && NIMBLE_CPP_VERSION_MAJOR >= 2
#define HUB_NIMBLE2 1
#else
#define HUB_NIMBLE2 0
#endif

namespace hub {
namespace blec {

// Raw advert (+ scan response) bytes.
inline const uint8_t *payload(const NimBLEAdvertisedDevice *d) {
#if HUB_NIMBLE2
  return d->getPayload().data();
#else
  return const_cast<NimBLEAdvertisedDevice *>(d)->getPayload();
#endif
}
inline size_t payloadLen(const NimBLEAdvertisedDevice *d) {
#if HUB_NIMBLE2
  return d->getPayload().size();
#else
  return const_cast<NimBLEAdvertisedDevice *>(d)->getPayloadLength();
#endif
}

// The 6 address bytes, least significant first (as NimBLE stores them).
inline const uint8_t *addrBytes(const NimBLEAddress &a) {
#if HUB_NIMBLE2
  return a.getVal();
#else
  return const_cast<NimBLEAddress &>(a).getNative();
#endif
}

// 16-bit UUID value (only meaningful when bitSize() == 16).
inline uint16_t uuid16(const NimBLEUUID &u) {
#if HUB_NIMBLE2
  return reinterpret_cast<const ble_uuid16_t *>(u.getBase())->value;
#else
  return const_cast<NimBLEUUID &>(u).getNative()->u16.value;
#endif
}

// Address from "aa:bb:cc:dd:ee:ff". NimBLE 2 needs the type: public unless
// told otherwise (1.4 guessed public too).
inline NimBLEAddress makeAddr(const std::string &s, uint8_t type = BLE_ADDR_PUBLIC) {
#if HUB_NIMBLE2
  return NimBLEAddress(s, type);
#else
  (void)type;
  return NimBLEAddress(s);
#endif
}

// Discovered services / characteristics as a plain list.
inline std::vector<NimBLERemoteService *> services(NimBLEClient *c, bool refresh) {
#if HUB_NIMBLE2
  return c->getServices(refresh);
#else
  return *c->getServices(refresh);
#endif
}
inline std::vector<NimBLERemoteCharacteristic *> chars(NimBLERemoteService *s, bool refresh) {
#if HUB_NIMBLE2
  return s->getCharacteristics(refresh);
#else
  return *s->getCharacteristics(refresh);
#endif
}

// Connect timeout: 1.4 takes seconds, 2.x milliseconds.
inline void setConnectTimeoutSec(NimBLEClient *c, uint32_t sec) {
#if HUB_NIMBLE2
  c->setConnectTimeout(sec * 1000);
#else
  c->setConnectTimeout(sec);
#endif
}

}  // namespace blec
}  // namespace hub
