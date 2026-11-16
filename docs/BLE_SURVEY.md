# Van visit: listening to Bluetooth

The `hub-esp32-survey` build turns the hub into a pure listener. It never
pairs, never connects and never sends anything (not even the "what's your
name?" scan request), so the Trek 3, BMPRO, Victrons and heater carry on
exactly as before. Everything it hears goes to the Surface over USB.

## Before you go (at home)

1. `firmware/hub/src/secrets.h` must have the van's Wi-Fi and a `WEB_USER` /
   `WEB_PASS` (the survey build refuses to build without a login).
2. In VictronConnect, for each SmartSolar: cog > Product info > Instant
   readout via Bluetooth: on > Instant readout details: Show. Copy the key.
   (These can also be entered at the van.)

## At the van

1. Plug the hub into the Surface (COM4) and flash + log in one go:

   ```
   cd firmware\hub
   pio run -e hub-esp32-survey -t upload
   pio device monitor -e hub-esp32-survey -f log2file
   ```

   The log is saved as `platformio-device-monitor-<date>.log` in `firmware\hub`.
2. On the phone, open the hub's page > Setup > Victron solar controllers,
   paste each key and press Find nearby. Within a few seconds the log shows
   `[victron] solar 1 ...: 13.42 V 5.1 A 68 W` lines.
3. Easiest: run the guided test instead of the monitor (close the monitor
   first, only one program can use COM4). Double-click
   `firmware\hub\tools\guided-survey.bat`. It tells you what to do one step
   at a time (pumps on/off 3 times, heater on, change temperature...), marks
   each step in the log, and shows which devices changed while you did it.
   Press Enter after each step, `s` to skip one, `l` for "set up later"
   (not fitted yet: it's left out of future runs), `q` to finish. Once the
   part is fitted, run only those steps: `guided-survey.bat COM4 --later`. The log is
   saved as `van-test-<date>-<time>.log` in `firmware\hub`.

   Without the script: use the van for 15-30 minutes while the monitor logs:
   switch a pump on and off, run a tap so a tank level moves, turn the heater on
   and off with its own remote/app, open the BMPRO app, noting the times.
4. Send the log file back.

## Reading the log

| Line | Meaning |
|---|---|
| `ADV ... tag=victron-solar` | A Victron. Encrypted; needs its key to read. |
| `[victron] solar 1 ...: 13.42 V ...` | A Victron decoded with its key: working. |
| `[victron] ... no saved key matches - its key starts with 3f` | Heard a Victron, but the key entered doesn't fit. Check the first two characters. |
| `ADV ... tag=bmpro?` | Name looks like a BMPRO part (OdysseyLink, CommLink, SmartSense, BC300). |
| `ADV ... tag=heater?` | Looks like a Bluetooth diesel heater (FFE0 service or "heater" in the name). |
| `ADV ... tag=thermometer temp=21.4` | A Bluetooth thermometer the hub can already read. |
| `ADV ... tag=?` | Unknown. Kept in case a BMPRO part doesn't advertise a name. |
| `SEEN ...` | Summary every 60 s, our devices first. |

Phones, laptops, earbuds and trackers are counted but not logged.

A device that only sends its name when asked won't show a name here, because
asking is a transmission. If the BMPRO or heater only appear as `tag=?`, they
can be picked out by RSSI (closest = strongest, e.g. -50) and by their data
changing when a pump or the heater is switched.
