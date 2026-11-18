# BMPRO Bluetooth protocol (as decoded so far)

From Beau's van on 2026-11-17: the hub's listen-only survey log, plus an
Android HCI snoop log of the BMPRO app (pumps 1 and 2 switched on/off 3 times),
checked against the app and Trek 3 screens.

## 1. Broadcasts: no connection needed (implemented, `BmproAdv.h`)

Device `f8:f0:05:fe:4b:35`, service data UUID `0xFE0E`, 18 bytes:

| Frame | Contents |
|---|---|
| `0x01` battery | mV (u16 LE @1), mA (i32 LE @3, negative = discharging), a mAh counter (u32 LE @7; not "Ah left"), `1b 18`, uptime s (@13) |
| `0x04` | bytes 16-17 matched the app's Solar 12.2 A once, but read 61.1 at night with no charging, so the meaning is unknown and it is not used; bytes 14-15 followed the battery current's size |
| `0x00` identity | constant bytes + uptime |

A neighbour's BMPRO (different model) sends 20-byte frames, which are ignored.

## 2. Connection to the OdysseyLink (`c8:a4:6d:be:45:2b`, name "ODL104")

GATT (SETEC = BMPRO's parent company):

| Handle | Characteristic | Use |
|---|---|---|
| 0x29 (CCCD 0x2a) | "SETEC AUTH" | Subscribe, and it notifies a 4-byte challenge (`e1 c0 7b 00`). The app writes 8 bytes (`a2 f1 4b 78 ef bf bd ef`), and it notifies "Authenticated". The challenge and answer were the same both times in this log. Whether that holds across sessions is unknown. |
| 0x2c (CCCD 0x2d) | "SETEC CAN BLE" | CAN-style parameter traffic (below) |
| 0x2f | "BLE HEARTBEAT" | |
| 0x1d (CCCD 0x1e) | (service 5e07bdf0-…) | The app writes 00..05, 0c..0f, 12, 13 once at start |
| 0x16 | firmware string "2.0.1-0-gd8bdd5a" | |

App start sequence: enable notifications on 0x2a, 0x2d, 0x1e, 0x26 and 0x22, then authenticate on 0x29, then write the 0x1d list, then write requests on 0x2c.

### 0x2c messages: `[op] 00 [node] [param] [len] [data…]`

- Ops seen: `3c` read request, `3e` write request, `3d` other (from the app); `18` value, `16` write reply, `14` info (from the BMPRO).
- Node `0x40` is the BMPRO charger/power unit; node `0x90` is the tank/sensor side.

| Node.param | len | Meaning (evidence) |
|---|---|---|
| 40.40-40.4d | 5 | Output channels: byte 0 = on/off, then u32 BE = current in mA. 40.40 ran about 3.7-5 A. |
| **40.4c** | 5 | **Pump 1**: state byte. Write `3e 00 40 4c 01 01` = on, `… 01 00` = off |
| **40.4d** | 5 | **Pump 2**: same, `3e 00 40 4d 01 01/00` |
| 40.61 | 4 | Battery current, mA, i32 BE (-2913 = app 2.9 A discharging) |
| 40.62 | 4 | Battery voltage, mV (13737 = 13.7 V) |
| 40.64 | 4 | Solar input, mA (12154 = app Solar 12.2 A) |
| 40.65 | 4 | Probably the charger output voltage, mV (13633) |
| 40.66 | 4 | **State of charge %** (99) |
| 40.67 | 4 | Time left, minutes (6831 = 113.8 h = app "113h left") |
| 40.6b, 40.6c | 4 | Unknown, slowly changing (about 284, about 14 900) |
| 40.68 | 4 | 7fffffff (n/a) |
| 90.80 | 4 | **Tanks**, probably: `00 02 04 04` = tank 4 empty, Grey 2/4, Bottom 4/4, Top 4/4 (app: Top Full, Bottom Full, Grey 1/2) |
| 90.83 | 5 | Alternates `01 ffff509c` / `02 ffff509c` every 5 s (unknown, maybe gas sensor slots) |

Values repeat every ~18 s while connected.
