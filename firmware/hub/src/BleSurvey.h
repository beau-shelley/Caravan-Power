// Bluetooth survey mode (build env hub-esp32-survey).
//
// For the first van visit: the hub only LISTENS. It scans passively (it never
// sends a scan request, never pairs, never connects) and prints every
// Bluetooth advert it hears to the USB serial port, so the BMPRO OdysseyLink,
// the Victrons and the diesel heater can be identified and their broadcasts
// captured without touching anything in the van. The BMPRO link is not
// started, and the commands that could talk to the BMPRO are refused.
//
// Every advert is labelled (cv::bleClassify): bmpro?, victron-solar...,
// heater?, thermometer, or ? for unknown. Phones, laptops and earbuds are
// counted but not logged, so a busy caravan park doesn't drown the log.
//
// Serial output (115200 baud), one line per advert whose payload changed
// (our devices at most once a second, unknown ones once every 5 s):
//   ADV <ms> <mac> rssi=<dBm> tag=<label> [name="..."] [temp=<C>] [mfr=<hex>] raw=<hex>
// and every 60 s a summary of every device heard so far, ours first:
//   SEEN <mac> tag=<label> rssi=<last> n=<adverts> payloads=<n> age=<s> [name="..."] [mfr_id=<hex>]
// Victrons with a key saved also log their decoded readings ([victron] lines).
#pragma once

namespace hub {
namespace survey {

#ifdef HUB_BLE_SURVEY
constexpr bool kOn = true;
#else
constexpr bool kOn = false;
#endif

void begin();  // starts the passive scan and the logger
void loop();   // prints the periodic summary

}  // namespace survey
}  // namespace hub
