# Protocol reference

All clients use the same JSON messages:
- the touchscreen, over RS485;
- the local web page, over a WebSocket on port 81 or `POST /api/cmd`;
- the cloud dashboard, over MQTT `caravan/<id>/cmd`.

## Transport details

**RS485** (115200 8N1) carries one JSON document per line, followed by `*HH\n`, where `HH` is the XOR of the JSON bytes. The hub is bus master:
- every 1 s it sends a `state` frame;
- every 250 ms it sends `{"t":"p"}` (a poll).

The display transmits only immediately after a hub frame ends, so the half-duplex bus never collides.

**Local WebSocket:** the browser first fetches `GET /api/token` (behind HTTP Basic auth), then sends `{"c":"auth","tok":"…"}`.

**MQTT:** add `"rid":"x"` to a command and the reply on `resp` echoes it.

## State (`{"t":"state", …}`)

| Key | Meaning |
|---|---|
| `ts`, `tz` | unix time, local UTC offset (s) |
| `bat` | `on`, `src` (1 shunt, 2 BMPRO), `v`, `a` (+charge), `soc`, `ttg` (min, -1 idle), `sa` solar A, `ca` charger A, `chg` total charge A, `load` load A |
| `sol[2]` | `on`, `pv` V (-1 when not available, i.e. over Bluetooth), `w` W, `bv`, `ba`, `cs` (VE.Direct charge state), `err`, `yt` kWh today, `mx` peak W today, `ytot` |
| `tmp[3]` | inside, outside, fridge: `on`, `c`, `lo`, `hi` (today), `b` sensor battery % (Bluetooth, if reported) |
| `tank[3]` | `on`, `p` % |
| `pump[2]` | `on`, `sn` sensed, `lk` dry-run lockout, `rt` run seconds |
| `inv` | `on`, `ac` (240 V present) |
| `ac` | `on`, `pre` preset index |
| `alerts[]` | `id`, `sev` (0 info, 1 warn, 2 alarm), `ack`, `since`, `val`, `msg` |
| `lvl` | `on`, `z` (level set), `f` (front learned), `p` pitch ° (+ front high), `r` roll ° (+ left high), `tr` track mm, `jk` axle-to-jockey mm. Also sent alone as `{"t":"lvl",…}` 4×/s on RS485 |
| `wx` | `ok`, `up` fetched (unix), `src` (1 GPS, 2 phone, 3 manual), `pl` place, `t` °C, `c` WMO code, `day`, `w`/`g` wind/gust km/h, `d[4]` = `[code, weekday 0=Sun, max, min, rain %, rain mm, gust]` |
| `net` | `wifi`, `cloud`, `ip` |

## Commands (`{"c": …}`)

| Command | Fields | Notes |
|---|---|---|
| `state`, `hello`, `rules_get`, `ping` | | read-only; `hello` returns meta + rules |
| `pump` | `i`, `on` | refused if locked out; with the relay fallback, also if the BMPRO turned it on |
| `inverter` | `on` | |
| `aircon` | `preset` or `off:true` | |
| `ack` | | acknowledge all alerts (silences buzzer) |
| `rule_set` | `rule:{id,en,name,m,cmp,thr,hys,dly,sev,ntf,act}` | `id:0` creates a new rule |
| `rule_del` | `id` | |
| `rules_reset` | | restore defaults |
| `ac_learn` | `slot` (-1 = OFF, n = new), `name` | then poll `ac_learn_status` |
| `ac_del` | `slot` | |
| `tank_cal` | `i`, `point` (`empty`/`full`), optional `mv` | omit `mv` to use the current reading |
| `soc_set` | `soc` | |
| `sensors` / `sensor_assign` | `addr`, `role` | wired DS18B20 role mapping |
| `location_set` | `lat`, `lon`, `name`, `src` (2 phone, 3 manual) | weather location (refused while a GPS has a fix) |
| `weather_refresh` | | fetch the forecast now |
| `level_zero` | | "Set level": averages 2 s and saves the mounting angle |
| `level_front` | | "Learn front": with the front lifted 2-3 cm |
| `level_reset`, `level_status` | | forget calibration / last calibration result |
| `level_dims` | `track`, `jockey` (mm) | wheel spacing, axle-to-jockey distance |
| `ble_sensors` | | Bluetooth thermometers in range (temp, battery, signal, assigned role) |
| `temp_set` | `role`, `src` (0 wired, 1 Bluetooth), `addr` | assign a Bluetooth thermometer to inside/outside/fridge |
| `remote_control` | `on` | **not accepted from the cloud** |
| `config_set` | `cfg:{…}` | partial config merge (see `HubConfig::toJson`) |
| `victron_set` | `i`, `key` (32 hex), optional `addr`, `src` (0 Bluetooth, 1 VE.Direct, 2 off) | add/replace a SmartSolar's Instant Readout key |
| `victron_seen` | | Victron devices in Bluetooth range and which are decoding |
| `bmpro_devices`, `bmpro_log` | | Bluetooth devices seen / OdysseyLink log |
| `bmpro_connect` | `addr` | set + remember the OdysseyLink address |
| `bmpro_write` | `uuid`, `hex`, `resp` | raw write, for replaying captured Odyssey app commands |
| `reboot` | | |

When remote control is off, every command except the read-only ones is refused from MQTT.

Rule metric keys: `bat_v bat_soc bat_a load_a charge_a solar_w solar1_w solar2_w temp_in temp_out temp_fridge tank1 tank2 tank3 wx_gust wx_rain` (the last two are today's forecast max gust and rain chance).
Actions: `none aircon_off inverter_off pumps_off`.

## Hub config (`GET/POST /api/config`)

`meta` (names, aircon presets, `rc`), `rules`, and the following:
- `bat`: `src` (2 BMPRO = default, 1 INA226 shunt), `cap` Ah, `eff`, `fullV`, `tailA`, `shunt` Ω, `chg`, `chgShunt`, `vCal`
- `tanks[]`: `empty`, `full` mV, `ch`, `en`
- `pumps[]`: `mode` (3 BMPRO over Bluetooth = default, 0 relay latch, 1 relay pulse, 2 disabled), `pulse` ms, `maxRun` min, `dryTank`, `dryPct`, `sense`
- `vic[]`: `src` (0 Bluetooth = default, 1 VE.Direct, 2 off), `addr`, `key`
- `tankSrc`: 0 BMPRO (default), 1 ADS1115 taps, 2 none
- `bmproAddr`: OdysseyLink Bluetooth address
- `inv`: `mode`, `pulse`, `sense`
- `loc`: `src`, `lat`, `lon`, `name` (weather location)
- `tsrc[]` (0 wired, 1 Bluetooth per role), `tble[]` (Bluetooth address per role)
- `tempAddr[]`, `cloudInt` s, `buzzer`, `bmproExplorer` (verbose Bluetooth logging)

## Bluetooth level service (hub as peripheral, used by `web/level.html`)

| UUID | Access | Content |
|---|---|---|
| `8d5c0001-6b0e-4e4f-9c6a-2f1c7a4e0001` | service | advertised by `caravan-hub` |
| `8d5c0002-…-2f1c7a4e0001` | read / notify (5 Hz) | 9 bytes LE: int16 pitch×100, int16 roll×100, uint8 flags (1 sensor ok, 2 level set, 4 front learned, 8 calibrating), uint16 track mm, uint16 jockey mm |
| `8d5c0003-…-2f1c7a4e0001` | write | text: `zero`, `front`, `reset`, `dims:<track>,<jockey>` |

