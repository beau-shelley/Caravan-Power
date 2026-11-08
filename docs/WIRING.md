# Wiring

Everything here is on the 12 V side except the optional inverter 240 V sense; read the safety notes in the README first.

**The BMPRO system and the Victron solar controllers need no wiring at all.** Battery, tank levels and pumps come over Bluetooth from the OdysseyLink (see [BMPRO_INTEGRATION.md](BMPRO_INTEGRATION.md)), and both SmartSolars broadcast their readings over Bluetooth. The core install is:
- power to the hub and display;
- the RS485 cable between them;
- 3 temperature probes;
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
| 3 | DS18B20 waterproof probes + 4.7 kΩ resistor | inside, outside, fridge |
| 1 | 1- or 2-channel 3.3 V-trigger relay module | inverter remote (+ spare) |
| 1 | 12 V opto-isolator board (PC817) | inverter on sense |
| 1 | 940 nm IR LED + 2N2222/BC337 + 33 Ω, TSOP38238 receiver | aircon |
| 1 | Active buzzer 3–5 V + NPN | local alarm |
| — | Fuses/holders, ferrules, heatshrink, enclosure | 2 A for the electronics |

Only needed for the wired fallbacks: VE.Direct pigtails (solar), ADS1115 + 3 dividers (tanks), INA226 + 300 A shunt (battery), relays + Schottky diodes + opto (pumps).

## Hub (ESP32-S3-DevKitC-1) pin map

| GPIO | Function | Goes to |
|---|---|---|
| 43 (TX) | RS485 DI | MAX3485 DI |
| 44 (RX) | RS485 RO | MAX3485 RO |
| 42 | RS485 DE+RE | MAX3485 DE and /RE tied together |
| 17 / 16 | VE.Direct #1 / #2 RX | *fallback only* |
| 8 / 9 | I²C SDA / SCL | *fallback only:* ADS1115 (0x48), INA226 (0x40 / 0x41) |
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

## Temperature sensors (DS18B20)

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
