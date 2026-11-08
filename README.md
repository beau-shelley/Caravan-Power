# Caravan Power

An ESP32 system that links all of the caravan's power, water and climate systems together. It shows them on a **7" Waveshare touchscreen** inside the van, serves a **web dashboard you can open from anywhere** over Starlink, and sends **push alerts to your phone**.

It is designed to sit *alongside* the existing BMPRO / Trek 3 setup, not replace it. The Trek screen, the BMPRO pump switching and the tank gauges all keep working as they do today.

| Touchscreen: home | Touchscreen: alarm |
|---|---|
| ![home](docs/img/01-home.png) | ![alarm](docs/img/06-home-alarm.png) |
| **Power** (per-MPPT tracking + 30 min history) | **Water** (tanks + pump control) |
| ![power](docs/img/02-power.png) | ![water](docs/img/03-water.png) |
| **Climate** (temps, aircon presets, inverter) | **Alerts** (active alerts + editable rules) |
| ![climate](docs/img/04-climate.png) | ![alerts](docs/img/05-alerts.png) |

Remote dashboard (desktop and phone, dark and light):

<img src="docs/img/web-desktop.png" width="640"> <img src="docs/img/web-phone-top.png" width="220">

> The screenshots are rendered from the real UI code (see [`tests/sim`](tests/sim)), with demo data.

## What it does

| Requirement | How |
|---|---|
| Battery %, load amps, charge amps | INA226 + shunt coulomb counter (BMPRO data later, once decoded) |
| Individual solar controller tracking | Both Victron SmartSolar 100/50s over **VE.Direct**: PV W/V, charge A, state, yield today, peak, errors |
| Inside / outside / fridge temps with alarms | 3 × DS18B20; min/max today; alarm rules with delay and hysteresis |
| Customisable alerts on temps, volts and amps | Rule engine: any metric, above/below, threshold, hysteresis, delay, severity, push on/off, plus an optional **automatic action** (aircon off, inverter off, pumps off) |
| Phone alerts | [ntfy](https://ntfy.sh) push (free iOS/Android app). Alarms use *urgent* priority |
| 2 water pumps on the touchscreen, BMPRO switching kept | Relay diode-OR'd with the BMPRO outputs, plus sense inputs. Dry-run lockout and a max run time |
| Water tank gauges | Taps the 3 existing sender lines through a high-impedance divider into an ADS1115. The Trek still reads them |
| Inverter control | Relay on the Powertech remote port (pulse or latch), with a 240 V output sense |
| Air-con control | Learns IR codes from the Houghton remote and replays them (up to 8 presets plus OFF) |
| Remote access over Starlink | Outbound MQTT over TLS, so it works behind Starlink CGNAT with no port forwarding |
| Modern UI on the 7" Waveshare | LVGL 8.3 dark UI, 800×480, nav rail plus 6 pages, screen sleep with wake on alarm |

## Architecture

```
                         Starlink ── internet ── MQTT broker (TLS) ── phone / laptop dashboard
                             │                        ▲                  (web/index.html)
                         Wi-Fi                        │ outbound only            ntfy push
                             │                        │                              ▲
 ┌───────────────────────────┴────────────────────────┴──────────────────────────────┴────┐
 │ HUB  ESP32-S3-DevKitC-1                                                                │
 │  VE.Direct ×2 ── SmartSolar 100/50 #1, #2       1-Wire ── DS18B20 inside/outside/fridge│
 │  I²C ── ADS1115 (tank sender taps) ── INA226 (battery shunt) [+ INA226 charger shunt]   │
 │  Relays ── pump 1, pump 2 (diode-OR with BMPRO), inverter remote                       │
 │  Opto inputs ── pump 1/2 running, inverter 240 V present                               │
 │  IR LED / IR receiver ── Houghton aircon      BLE ── BMPRO CommLink (capture tool)     │
 │  Local web UI http://caravan.local  +  WebSocket                                       │
 └──────────────────────────────┬─────────────────────────────────────────────────────────┘
                                │ RS485 (4-core: 12 V, GND, A, B)
 ┌──────────────────────────────┴───────────────┐
 │ DISPLAY  Waveshare ESP32-S3-Touch-LCD-7      │      BMPRO 35A / Odyssey Link 105 /
 │  800×480 LVGL touchscreen                    │      CommLink / Trek 3 keep working
 └──────────────────────────────────────────────┘      exactly as before
```

**Why two ESP32s?** On the Waveshare 7" board almost every GPIO drives the RGB panel. The only spare I/O is the RS485 port, I²C and a CAN port. So the screen is a pure UI and a cheap ESP32-S3 dev board does all the wiring-heavy work. The wired RS485 link keeps working when Wi-Fi or Starlink is off.

Everything speaks one JSON format (`firmware/lib/CaravanCommon`), whether over the RS485 link, the local WebSocket or MQTT. So the touchscreen, the local web page and the remote dashboard all see the same data and send the same commands.

## Repository layout

```
firmware/
  lib/CaravanCommon/   shared: data model, JSON format, alert engine, VE.Direct parser,
                       SoC estimator, RS485 framing (portable C++, host-tested)
  hub/                 PlatformIO project - ESP32-S3 hub
  display/             PlatformIO project - Waveshare ESP32-S3-Touch-LCD-7
web/index.html         dashboard (local + cloud), single file, no build step
tests/                 host unit tests (make -C tests) + UI simulator (tests/sim)
docs/                  wiring, cloud setup, BMPRO notes, protocol reference
```

## Quick start

1. **Wire it up.** Follow [docs/WIRING.md](docs/WIRING.md). Start with the RS485 link, the Victrons and the temperature sensors: they need no changes to the existing wiring.
2. **Set up the cloud side** (optional; the system runs fully offline without it) using [docs/CLOUD_SETUP.md](docs/CLOUD_SETUP.md): a free HiveMQ Cloud broker plus the ntfy app.
3. **Flash the hub.**
   ```sh
   cd firmware/hub
   cp src/secrets.example.h src/secrets.h    # Wi-Fi, MQTT, ntfy topic, web password
   pio run -t upload                          # firmware
   pio run -t uploadfs                        # web dashboard -> LittleFS
   pio device monitor                         # watch it find sensors
   ```
4. **Flash the display.**
   ```sh
   cd firmware/display
   pio run -t upload
   ```
5. **Commission it** from the touchscreen, under **Settings**:
   - Calibrate each tank (*Set EMPTY* / *Set FULL*). Do one end now and the other when it next happens.
   - Learn the aircon presets: set the Houghton remote to e.g. *Cool 22 auto*, tap *Learn preset* and press the remote's button. Repeat for each mode you use, then do *Learn OFF*.
   - Check the temperature sensors went to the right roles. Use the web dashboard → *Setup* → *Scan 1-Wire bus* to reassign them.
   - When the battery is next full, set SoC to 100% (Power page). After that it syncs automatically.
6. **Tune the alert rules** on the Alerts page (+/− thresholds) or in the dashboard (full editor). The defaults suit 12 V lithium: see `loadDefaultRules()` in [`CaravanModel.cpp`](firmware/lib/CaravanCommon/src/CaravanModel.cpp).

## BMPRO integration status

BMPRO does not publish the protocol for the CommLink or the Trek / Odyssey Link bus. So in this first version:

- **Battery %, volts and amps** come from an INA226 module and a 300 A / 75 mV shunt on the battery negative.
- The hub includes a **BLE capture tool** for the CommLink. Once its frames are decoded, the Odyssey Link's own battery data, and possibly pump control over the bus, can replace the extra shunt by filling in one function (`Bmpro::decode`).

See [docs/BMPRO_INTEGRATION.md](docs/BMPRO_INTEGRATION.md) for the step-by-step capture procedure.

## Testing

```sh
git clone --depth 1 -b v7.2.1  https://github.com/bblanchon/ArduinoJson ../ArduinoJson
git clone --depth 1 -b v8.3.11 https://github.com/lvgl/lvgl ../lvgl
make -C tests                 # 77 unit checks under ASan/UBSan: VE.Direct parser, alert engine,
                              # JSON round-trip, SoC estimator, RS485 framing
make -C tests/sim             # builds the real display UI against LVGL and renders every page to PNG
```

Open `web/index.html?demo` in a browser to try the dashboard with fake data.

## Safety

- **240 V:** In Australia, any work on the 230 V side (the inverter output sense) must be done by a licensed electrician (AS/NZS 3001). Everything else here is 12 V.
- **Fusing:** Fuse every new 12 V feed at the source: 2 A for the hub/display, and the pump-rated fuse for each pump relay feed.
- **Load shedding:** Automatic actions only ever turn things **off**. Nothing is switched on without a person pressing a button.
- **Remote control:** It can be disabled from the touchscreen (Settings → *Allow remote control*). That switch can't be changed from the cloud.
