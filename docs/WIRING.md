# Wiring

Everything here is on the 12 V side except the optional inverter 240 V sense; read the safety notes in the README first.

**The BMPRO system and the Victron solar controllers need no wiring at all.** Battery, tank levels and pumps come over Bluetooth from the OdysseyLink (see [BMPRO_INTEGRATION.md](BMPRO_INTEGRATION.md)), and both SmartSolars broadcast their readings over Bluetooth. The core install is:
- power to the hub and display;
- the RS485 cable between them;
- temperature sensors: Bluetooth (no wiring) or wired probes;
- the inverter relay;
- the IR LED.

The tank taps, battery shunt and pump relays are kept at the end of this page as **optional fallbacks**.
Pin numbers are defined in [`firmware/hub/src/pins.h`](../firmware/hub/src/pins.h). Change them there if you need to.

## Parts list

| Qty | Part | Notes |
|---|---|---|
| 1 | Waveshare **ESP32-S3-Touch-LCD-7** | you already have it |
| 1 | **ESP32-S3-DevKitC-1** (N8R2 or N8R8) | the hub |
| 1 | 12 V → 5 V 3 A automotive buck converter | powers the hub (and the display if your board has no 7–36 V input) |
| 1 | MAX3485 / "TTL to RS485" 3.3 V module | hub side of the display link (an auto-direction module is fine; set `PIN_LINK_DE` to -1) |
| 1 | 4-core shielded cable (e.g. 0.5 mm²) | hub ↔ display: 12 V, GND, RS485 A, B |
| 2-3 | Bluetooth thermometers (see *Temperature sensors*) | e.g. 2-3 × ThermoPro TP357 + 1 × Shelly BLU H&T |
| 0-3 | DS18B20 waterproof probes + 4.7 kΩ resistor | only for roles you want wired |
| 1 | 1- or 2-channel 3.3 V-trigger relay module | inverter remote (+ spare) |
| 1 | 12 V opto-isolator board (PC817) | inverter on sense |
| 1 | 940 nm IR LED + 2N2222/BC337 + 33 Ω, TSOP38238 receiver | aircon |
| 1 | Active buzzer 3–5 V + NPN | local alarm |
| 1 | MPU-6050 (GY-521) module | caravan levelling |
| — | Fuses/holders, ferrules, heatshrink, enclosure | 2 A for the electronics |

Only needed for the wired fallbacks: VE.Direct pigtails (solar), ADS1115 + 3 dividers (tanks), INA226 + 300 A shunt (battery), relays + Schottky diodes + opto (pumps).

## Hub (ESP32-S3-DevKitC-1) pin map

| GPIO | Function | Goes to |
|---|---|---|
| 43 (TX) | RS485 DI | MAX3485 DI |
| 44 (RX) | RS485 RO | MAX3485 RO |
| 42 | RS485 DE+RE | MAX3485 DE and /RE tied together |
| 17 / 16 | VE.Direct #1 / #2 RX | *fallback only* |
| 8 / 9 | I²C SDA / SCL | MPU-6050 level sensor (0x68); *fallbacks:* ADS1115 (0x48), INA226 (0x40 / 0x41) |
| 4 | 1-Wire | all DS18B20 data lines + 4.7 kΩ pull-up to 3V3 |
| 10 | Pump 1 relay | *fallback only* |
| 11 | Pump 2 relay | *fallback only* |
| 12 | Inverter relay | relay IN3 |
| 13 | Spare relay | relay IN4 |
| 14 | Pump 1 running sense | *fallback only* |
| 21 | Pump 2 running sense | *fallback only* |
| 47 | Inverter 240 V sense | opto output / AC detect module output |
| 5 | IR LED driver | transistor base via 1 kΩ |
| 6 | IR receiver | TSOP38238 OUT (powered from 3V3) |
| 7 | Buzzer | transistor base via 1 kΩ |
| 39 | GPS RX | GPS module TX (optional; shares UART2 with the VE.Direct #2 fallback) |
| 40 | GPS TX | GPS module RX (optional, not needed) |

Do not use GPIO 0, 3, 45 or 46 (strapping pins), 19/20 (USB), 26–37 (flash/PSRAM) or 48 (on-board LED).

## Power

```
Battery + ── 2 A fuse ── buck 12→5 V ── hub 5V pin
                              └─────── display (USB-C 5 V) ── or ── display 7-36 V input (newer boards)
Battery - (system side of shunts) ── hub GND ── display GND
```

The hub and display draw roughly 0.15 A and 0.4 A at 12 V; the display backlight is most of it, and it sleeps when idle.

## RS485 link to the display

The Waveshare board has an on-board auto-direction RS485 transceiver on GPIO15 (TX) / GPIO16 (RX), brought out on its screw terminal.

```
Hub MAX3485 A ─────────────── Display RS485 A
Hub MAX3485 B ─────────────── Display RS485 B
Hub GND       ─────────────── Display GND        (keep a common ground)
```

For runs over ~10 m, put 120 Ω across A–B at each end. If nothing arrives, swap A and B first; some modules label them the other way around.

## Victron SmartSolar 100/50 (×2) - Bluetooth

No cables. SmartSolar controllers broadcast their live readings over Bluetooth ("Instant Readout"), encrypted with a key unique to each controller. The hub listens passively: no pairing, nothing connects to the controllers, and VictronConnect on your phone keeps working.

For each controller:

1. **Turn on Instant Readout:** in **VictronConnect**, open the controller → cog (⚙) → ⋮ menu → **Product info**, and make sure **Instant readout via Bluetooth** is *Enabled*.
2. **Copy the key:** on the same screen, tap **Instant readout details → Show** and copy the **encryption key** (32 hex characters). The MAC address shown there is optional.
3. **Paste it into the hub:** open the dashboard (`http://caravan.local`) → **Setup** → *Victron solar controllers*, paste the key into the right row (Roof / Portable), and press **Save**.
4. **Check it's working:** press **Find nearby**. The controller should show "decoding as Roof" (or Portable), and its watts appear on the touchscreen within a few seconds.

What Bluetooth gives you, per controller:
- PV power (W), charge current, battery voltage;
- charge state (Bulk / Absorption / Float…) and error code;
- yield today;
- peak watts today (tracked by the hub).

**Not included** in the broadcast: PV *voltage* and lifetime yield. If you want those, use the VE.Direct cable fallback at the end of this page for that controller.

**Range:** the hub needs to hear the controllers. Inside a caravan this is normally fine. If a controller is in a metal locker and shows offline, mount the hub closer or use VE.Direct for that one.

## Temperature sensors

Each role (inside, outside, fridge) can be either a **Bluetooth thermometer** or a **wired DS18B20 probe**, and you can mix them. Assign them in the dashboard: **Setup** → *Temperature sensors* → *Find Bluetooth thermometers*, then pick a role next to each sensor. Warm a sensor in your hand to tell which is which.

### Bluetooth thermometers

Any of these work out of the box. They broadcast unencrypted readings, so there's no pairing, and their own phone apps keep working.

| Sensor | Good for | Notes |
|---|---|---|
| **ThermoPro TP357** | Fridge, inside | **Magnetic back** + stand, small screen. **1 × AAA** (copes with fridge cold far better than a coin cell). Usually sold in 2-3 packs. |
| **Shelly BLU H&T** | Outside | Rated IP54 (dust/water resistant), -20 to 60 °C, ~3-year battery, 3M tape mount. Uses the open BTHome format. Leave its encryption **off**. |
| **Govee H5075** | Inside | Big screen, tabletop. The manual's operating range is 0-50 °C, so **not for the fridge**. |
| **Xiaomi LYWSD03MMC** with [pvvx firmware](https://github.com/pvvx/ATC_MiThermometer) | Inside (cheapest) | A few dollars each. Flash it once from a web page on your phone. Coin cell, so not ideal in the fridge. |

**My picks:**
- **Fridge and inside:** a 3-pack of **ThermoPro TP357**.
- **Outside:** a **Shelly BLU H&T**, mounted under the van out of direct sun and rain.

**Where magnets help (and where they don't):**
- **Fridge:** the sensor must sit *inside* the fridge, and most caravan fridges have plastic liners. Put the TP357 on a shelf or door bin, or stick a small steel disc inside for the magnet.
- **Outside:** the steel **chassis rails** and A-frame are ideal for a magnetic sensor. Caravan walls are aluminium or composite, so magnets won't stick to them.
- **Not in direct sun:** a sensor in the sun reads 10-20 °C high.

**Fridge reception:** a fridge is an insulated metal box, which weakens Bluetooth a lot. Most sensors still get through the door seal over a few metres. Mount the hub near the fridge if you can.

The hub marks a Bluetooth sensor offline only after 10 minutes of silence, so occasional missed packets don't matter. If a sensor can't be heard at all, use a wired DS18B20 probe for that role (below).

**Batteries:**
- **Low-battery warning:** the dashboard and the Climate page show a battery warning when a sensor reports ≤ 20 %. Shelly, Govee and Xiaomi/pvvx report their battery level; the ThermoPro doesn't.
- **Cold:** alkaline AAA cells lose capacity at fridge temperatures but work fine. **Lithium AAA** cells last much longer in the cold.

The ThermoPro decoder follows the format used by Home Assistant's ThermoPro integration, but I haven't tested it against a real unit. If its reading looks wrong, *Find Bluetooth thermometers* shows the raw values for comparison.

### Wired DS18B20 probes

All three sensors share GPIO4: red → 3V3, black → GND, yellow → GPIO4, plus one 4.7 kΩ resistor from GPIO4 to 3V3.

- **Fridge:** run the probe through the door seal or the vent, and tape it to a shelf away from the evaporator plate.
- **Outside:** mount it in the shade, under the van or the A-frame storage, not in direct sun.
- **Inside:** mount it at head height, away from the aircon outlet and the fridge vent.

On first boot, sensors are assigned to Inside, Outside and Fridge in discovery order. Reassign them from the dashboard: *Setup* → *Scan 1-Wire bus*. Warm each probe in your hand to identify it.

## Inverter (Powertech 3000 W)

The Powertech pure sine inverters have a **remote on/off port**. Check your manual for its pin-out and type:

- **Momentary remote button:** pulse mode (default). Wire the relay's dry contact across the button contacts, and leave `inverter.pulse` at 500 ms.
- **Latching remote switch:** set `inverter.mode = 0` (latch).

**Feedback:** the hub needs to know whether the inverter is actually on. Pick one of these:
- *Simplest:* sense the 12 V that powers the remote's LED, or the inverter's fan, with an opto channel.
- *Best:* a certified 230 V AC-detect module on the output, installed by a licensed electrician.

## Air conditioner (Houghton Belaire) - IR

```
5 V ── 33 Ω ── IR LED (940 nm) ── C   2N2222   E ── GND
                                       B ── 1 kΩ ── GPIO5
TSOP38238: VS ── 3V3, GND ── GND, OUT ── GPIO6
```

Aim the IR LED at the aircon's receiver window (on the ceiling unit). A little reflection off the ceiling is usually enough. Mount the TSOP receiver somewhere you can point the original remote at while learning.

IR is one-way: the hub only knows what it last sent. If someone uses the original remote, the screen will be out of step until the next touchscreen command.

## Level sensor (MPU-6050)

The MPU-6050 (usually sold as a "GY-521" board) shares the hub's I²C bus:

```
GY-521 VCC ── 3V3      GND ── GND      SDA ── GPIO8      SCL ── GPIO9      (AD0 unconnected = 0x68)
```

**Mounting:**
- **Screw it down solid:** bolt it to the van structure, either inside the hub's enclosure or on a floor or wall panel. A board dangling on its wires gives a wandering reading.
- **Any angle is fine:** it doesn't need to be level or square to the van, because calibration cancels the mounting angle out.
- **Keep the wires short:** under ~30 cm of I²C cable. If the sensor has to sit further away, use shielded or twisted wire.
- **Easiest option:** if you can, point the board's printed **X arrow** towards the front (hitch). Then *Learn front* is optional.

**Calibrating** (once; it's saved and survives reboots):
1. **Set level:** park somewhere you've checked is level with a spirit level on the floor inside, in both directions, then press **Set level** on the touchscreen's *Level* page or the phone. Keep still for 2 seconds.
2. **Learn front** (needed if the sensor isn't roughly square to the van): wind the jockey up so the front is 2–3 cm high, then press **Learn front**. The hub learns which way the van faces from the direction the reading moves.
3. **Enter dimensions:** the distance between the wheel centres and from the axle to the jockey wheel. These turn angles into "raise the left wheel 30 mm" (phone page → *Caravan dimensions*).

**Using it:** the touchscreen *Level* page and the phone page both show a bubble that floats to the high side, plus how many millimetres to raise a wheel (onto ramps or blocks) and to wind the jockey up or down. Do the wheels first, then the jockey.

**On your phone, over Bluetooth** (no Wi-Fi or internet needed at the site):
- **Android:** open `level.html` in **Chrome**.
- **iPhone:** Safari can't use Web Bluetooth, so open it in the free **Bluefy** browser.

Host the page with GitHub Pages (see [CLOUD_SETUP.md](CLOUD_SETUP.md)). Open it once while you have internet and it's cached for offline use, and *Add to Home Screen* makes it an app icon. Tap **Connect** and pick `caravan-hub`.

## Weather location (no GPS needed)

The forecast needs to know where the van is. **No GPS module or roof penetration is needed.** Set it whichever way is handy when you arrive:

| How | Where | Notes |
|---|---|---|
| **Phone level page** | `level.html` → **📍 Set weather location here** | One tap while you're levelling anyway. Your phone's GPS works without internet, and the location goes to the hub over Bluetooth. |
| **Town search on the touchscreen** | *Settings* → *Weather location* → **Search town** | Type e.g. "Mudgee" and pick from the list. The hub looks it up, so it needs Starlink up. |
| **Dashboard** | *Setup* → *Weather location* | Phone location or town search. |

**Saved until you change it:** the location survives reboots, so you only set it at each new site. The hub then fetches the forecast from Open-Meteo every 30 minutes whenever it has internet, about 2 KB a time. The last forecast stays on screen while you're offline, and the touchscreen shows how old it is.

<details>
<summary>Optional: GPS module (fully automatic, but needs sky view)</summary>

If you ever want it automatic, a u-blox NEO-6M/M8N module works on GPS TX → GPIO39 (3V3, GND). Its antenna needs a view of the sky, through plastic, fibreglass or glass but not a metal roof. A GPS fix takes priority over the other methods. It shares UART2 with the VE.Direct #2 fallback.
</details>

## Buzzer

Active buzzer + NPN on GPIO7. It beeps every 2 s while an **alarm**-severity alert is unacknowledged. Disable it with `buzzer: false` in the config.

## Optional wired fallbacks

Use these only if the OdysseyLink's Bluetooth protocol can't be decoded, or you want a reading that doesn't depend on the BMPRO. Change the source in the hub config and reboot.

### Victron VE.Direct cable (instead of Bluetooth)

Set that controller's source with `{"c":"victron_set","i":0,"src":1}` (or `"vic":[{"src":1}]` in the config). Wire GPIO17 (controller 1) / GPIO16 (controller 2).

The VE.Direct socket is a JST-PH 2.0 4-pin connector: **1 GND, 2 RX (into Victron), 3 TX (out of Victron), 4 +5 V**.

Only GND and TX are needed. Measure pin 3 against pin 1 with a multimeter while the controller is idle:

- **~5 V:** use a divider: 10 kΩ from Victron TX to the ESP pin, then 20 kΩ from the ESP pin to GND.
- **~3.3 V:** connect through a 1 kΩ series resistor.

Do **not** connect pin 4 (+5 V) to anything. The controllers share the battery negative with the hub, so no isolation is needed.

### Tank level senders (ADS1115 taps)

Set `"tankSrc": 1`. The BMPRO keeps powering and reading the senders; we only **listen** to the voltage on each sender wire through a high-impedance divider, so the BMPRO readings are unaffected:

```
Sender wire (at BMPRO tank input) ── 100 kΩ ──┬── ADS1115 A0 (A1, A2 for tanks 2, 3)
                                              33 kΩ      100 nF
                                               │          │
                                              GND        GND
```

Power the ADS1115 from 3V3. The divider scales up to ~13 V down to ≤3.2 V, and the 133 kΩ load is negligible to the BMPRO's sender circuit.

Calibrate each tank from the touchscreen (*Settings* → *Water tank calibration*) when it is empty and when it is full. Senders where the voltage goes *down* as the tank fills are handled automatically. If the BMPRO uses probe-type (stepped) senders, readings will move in steps, which is normal.

### Battery shunt (INA226)

Set `"bat": {"src": 1}`.

```
Battery - ── [300 A/75 mV shunt] ── all system negatives (Odyssey Link shunt, BMPRO, inverter, solar...)
              │IN+           │IN-
              └── INA226 ────┘     VBUS ── 1 A fuse ── Battery +
```

- **Everything must go through it:** every load and every charger must be on the system side. The Odyssey Link's own shunt can stay; just put the two in series.
- **Remove the 0.1 Ω (R100) resistor** on the INA226 module and run twisted sense wires to the shunt's small screws.
- **IN+ goes on the battery side,** so charging reads positive. If the sign is backwards, swap IN+ and IN-.
- **Set the shunt value:** `bat.shunt` in the config (default 0.00025 Ω for 300 A / 75 mV).
- **Optional charger channel:** a second INA226 (address 0x41, A0 pin high) on a 50 A shunt in the BMPRO 35A output shows mains/DC-DC charge current separately. Set `bat.chg = true`.

### Water pumps via relays

Set the pump's `"mode": 0`. The BMPRO switches each pump with its own output; we add a second, independent feed and combine the two with diodes (a "diode-OR"). Either side can then run the pump, and neither can back-feed the other:

```
BMPRO pump output ──|>|── (Schottky) ──┬── Pump +
                                       │
Fused +12 V ── relay COM/NO ──|>|──────┘
                                       └── opto input (+) ──> sense GPIO   (opto - to GND)
```

- **BMPRO / Odyssey app on** → pump runs. The sense input sees 12 V, and the screen shows the pump as on.
- **Touchscreen on** → relay closes → pump runs. The Odyssey app won't show it, because the BMPRO doesn't know.
- **Turning a pump off:** each side can only turn off what it turned on. The touchscreen tells you if the BMPRO has it on.
- **Dry-run protection:** the hub turns *its* relay off if the linked tank drops below 5%. Each pump also has a maximum run time (30 min by default). Both are configurable per pump.

**Alternative:** if your BMPRO unit has an external pump switch input (some JHub models do), set the pump `mode` to `1` (pulse). Wire the relay's dry contact across that input instead. The BMPRO then stays the single source of truth, and the Odyssey app shows the touchscreen's changes too.
