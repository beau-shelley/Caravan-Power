# Diesel heater

Beau's van has a Power Science 2 kW diesel heater with a 433 MHz remote. The
2 kW model very likely also does Bluetooth through the **AirHeaterBLE** phone
app (still to be checked at the van). The hub controls it from the touchscreen's
Climate page, the phone web page and the Android app: on/off, thermostat or
power level, set point, and status.

## How the hub reaches it

| Link | State | What you get |
|---|---|---|
| Bluetooth (AirHeaterBLE protocol) | written, **protocol confirmed** from an HCI snoop of Beau's heater (2026-11-18) | on/off, mode, set point, phase, error, cabin + heat-exchanger temperature, supply voltage |
| 433 MHz remote through a CC1101 module in the hub box | **stub only** (`Heater433` in `firmware/hub/src/Heater.cpp`) | on/off and set point only, no status (the remote is one-way) |
| Fake-data build (`hub-esp32-demo`) | simulated | behaves like a Bluetooth heater, sped up: 40 s to light, 30 s to cool |

Set it up on the phone web page: Setup > Diesel heater > Find nearby > Use >
Save. Close the AirHeaterBLE app first: the heater takes one Bluetooth
connection at a time. Until a heater is set up, the heater card stays hidden
everywhere.

## The protocol

This follows the vevor-ble-bridge project and was confirmed on Beau's heater
(69:96:1c:04:0b:f0) from an Android HCI snoop of the AirHeaterBLE app on
2026-11-18: status poll, on/off, mode, set temperature, supply voltage and the
cabin (room) temperature all decode as below. The hub uses the heater's cabin
temperature as the van's inside temperature when no inside thermometer is set up. It all
lives in `firmware/lib/CaravanCommon/src/AirHeater.{h,cpp}`, so corrections
touch nothing else.

- GATT service `FFE0`, one characteristic `FFE1` (write + notify).
- Commands are 8 bytes: `AA 55 <pass/100> <pass%100> <cmd> <arg lo> <arg hi> <checksum>`, checksum = sum of bytes 2-6. Passkey default 1234. cmd 1 status, 2 mode (1 level, 2 thermostat), 3 on/off, 4 set point.
- Status notifications start `AA 55`: running, error code, step (0 standby, 1 self test, 2 ignition, 3 running, 4 cool down), mode, set point, level, supply voltage (0.1 V), case temperature, cabin temperature. Byte offsets are in `AirHeater.h`.
- Error codes 1-10 are read as the usual E-01..E-10 (no start, out of fuel, voltage, sensors, fuel pump, fan, glow plug, overheat).
- Some newer heaters send an encrypted `AA 66` frame instead. Those are logged but not decoded.

## Confirming it at the van

The listen-only survey build (`hub-esp32-survey`) only hears adverts, so it can
tell us the heater's name, address and whether it advertises `FFE0`, but not
the command or status frames (those need a connection, which the survey build
never makes). To confirm the protocol:

1. From the survey log, note the heater's address and name.
2. Later, with the normal hub build, set the heater up (Setup > Diesel heater)
   and press **Show log**: every distinct status frame is listed (`RX ...`),
   plus every command sent (`TX ...`). If the heater doesn't have `FFE0`/`FFE1`
   the log lists the services it does have.
3. Or capture an Android Bluetooth HCI snoop log while using the AirHeaterBLE
   app (start, stop, change the temperature) and share it.

If the frames differ, only `AirHeater.cpp` (and its tests in
`tests/test_common.cpp`) need to change.

## Safety notes

- The hub never starts the heater on its own; only a tap on a screen does.
- Commands from the cloud dashboard follow the existing "Allow control from
  the cloud dashboard" switch.
- A heater fault (e.g. out of fuel) is pushed to the phone once.
