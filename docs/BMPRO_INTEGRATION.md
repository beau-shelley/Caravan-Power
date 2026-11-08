# BMPRO integration (wireless, via the OdysseyLink)

## The plan

The **OdysseyLink** is BMPRO's Bluetooth gateway. The free Odyssey phone app uses it to show battery state and tank levels and to **switch the water pumps**. The hub can make the same Bluetooth connection, which removes almost all the extra wiring:

| Item | Comes from | Wiring needed |
|---|---|---|
| Battery %, volts, amps | OdysseyLink over Bluetooth | none |
| Tank levels (3) | the tank senders already connected to the BMPRO system, relayed by the OdysseyLink | none |
| Pump 1 / 2 on/off and status | Commands over Bluetooth; the BMPRO switches the pumps itself | none |
| Trek 3 | Replaced by the 7" touchscreen | remove it (see the test below) |
| Solar (2 × Victron) | VE.Direct cables | 2 small cables |
| Temps, inverter, aircon | DS18B20s, relay, IR LED | as in WIRING.md |

The OdysseyLink accepts several Bluetooth connections at once (BMPRO says up to four), so the Odyssey app on your phone keeps working alongside the hub. That gives you a backup way to run the pumps if the hub is ever down.

All of this is the **default configuration** (`bat.src = 2`, `tankSrc = 0`, pump `mode = 3`). The wired options from the first version (INA226 shunt, ADS1115 tank taps, pump relays) are still in the firmware as fallbacks; see the bottom of this page.

## What has to happen first: decode the protocol

BMPRO doesn't publish the OdysseyLink's Bluetooth protocol, and I couldn't find anyone who has published a decode. **Until it's decoded, battery, tanks and pumps show "--" / offline.** Solar, temperatures, inverter, aircon and alerts work regardless.

The hub has everything needed to decode it. Afterwards, two functions get filled in, both in [`firmware/hub/src/Bmpro.cpp`](../firmware/hub/src/Bmpro.cpp):
- `Bmpro::decode()`: Bluetooth frames → battery, tank and pump values.
- `Bmpro::encodePump()`: pump on/off → the bytes to send.

### Step 1: connect the hub to the OdysseyLink

1. Turn on verbose logging: POST `{"bmproExplorer": true}` to `http://caravan.local/api/config`, then reboot the hub.
2. The hub scans. Open the dashboard → **Setup** → *BMPRO OdysseyLink*, or send `{"c":"bmpro_devices"}`. Find the OdysseyLink's address (name contains BMPRO / Odyssey).
3. Enter the address and press **Connect**. The hub saves it, reconnects automatically from then on, and logs:
   - every service and characteristic (R = read, W = write, N = notify, I = indicate);
   - the value of every readable characteristic;
   - every notification, as hex.

### Step 2: capture what the Odyssey app does (Android)

The easiest way to learn the protocol is to record the official app talking to the OdysseyLink:

1. **Enable logging:** on an Android phone, *Settings* → *Developer options* → **Enable Bluetooth HCI snoop log**. Toggle Bluetooth off and on.
2. **Record known actions:** open the Odyssey app and do things you can identify. Write down the time of each:
   - pump 1 on, wait 10 s, pump 1 off; then the same for pump 2;
   - leave the battery screen and the tank screen open for a minute each.
3. **Get the log:** fetch it with `adb bugreport` (it's in `FS/data/misc/bluetooth/logs/btsnoop_hci.log`) and open it in **Wireshark**. Filter on `btatt`.
4. **Read it:**
   - *Write Request / Write Command* entries are what the app sends. The ones at your pump timestamps are the pump commands, so note the characteristic handle/UUID and the bytes.
   - *Handle Value Notification* entries are the data coming back. Look for bytes that match the battery % and tank % shown in the app.

No Android phone? Use the hub's own log instead: switch pumps in the Odyssey app on your phone while the hub is connected, and watch which notification bytes change in the hub's log.

### Step 3: test the pump command from the hub

Replay a captured write and watch the pump:

```json
{"c":"bmpro_write", "uuid":"<characteristic uuid>", "hex":"<bytes from the capture>", "resp":true}
```

Send it with `POST /api/cmd` or the WebSocket. If the pump switches, put those bytes into `encodePump()`.

> **If the replay does nothing,** the app may first send a login/handshake write after connecting (look for writes right after the connection in the capture). Replay that first. If a value looks random each session, it's an authentication challenge, and that needs more work. Please share the capture and we can work through it.

### Step 4: write the decoder

`decode()` gets every notification. Map the bytes you identified onto `BmproData`; the source has a commented template. Check the results against the Odyssey app. Common patterns:
- 16-bit little-endian values;
- volts in 0.01 V units;
- signed current in 0.1 A or 0.01 A units;
- % as a single byte.

Once `decode()` returns data, the touchscreen fills in automatically. You can then turn `bmproExplorer` back off.

## Removing the Trek 3

The Trek connects to the BatteryPlus35 by cable and is one of two ways to see and control the system. The other is the OdysseyLink and app. I could not confirm from BMPRO's documentation that everything keeps working without the Trek, so **test it before you take it out**. The test is free and fully reversible:

1. **Note the Trek's settings** in set-up mode, especially battery type and tank configuration. You'll want them if you ever re-fit it.
2. **Unplug the Trek's cable** at the back of the Trek (leave the BatteryPlus35 end alone).
3. **Test with the Odyssey app on your phone,** over a few days:
   - battery values update;
   - all 3 tank levels show;
   - **pump 1 and pump 2 switch on and off;**
   - the charger still charges from mains/solar.
4. **If it all works,** leave the Trek out. The touchscreen plus the app replace it. Keep the Trek and its cable in a drawer for re-configuring or warranty work.
5. **If something stops** (e.g. the pumps need the Trek), plug it back in and ask BMPRO's Product Specialist team (<https://teambmpro.com/technical-support>) whether it's needed in your configuration. You can also mount it out of sight instead of removing it.

**Don't remove the Trek until step 3 passes and the hub has decoded data** (battery, tanks and pumps on the touchscreen). Until then, the Trek is your only on-wall way to run the pumps.

## Wired fallbacks (only if Bluetooth can't be decoded)

| Fallback | Config | Hardware (see WIRING.md) |
|---|---|---|
| Battery from own shunt | `"bat": {"src": 1}` | INA226 + 300 A shunt |
| Tanks from sender taps | `"tankSrc": 1` | ADS1115 + dividers (then calibrate on screen) |
| Pumps via relays | pump `"mode": 0` | relay + Schottky diode-OR + opto sense |

Reboot the hub after changing sources.
