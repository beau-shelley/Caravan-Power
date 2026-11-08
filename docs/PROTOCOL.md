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
| `sol[2]` | `on`, `pv` V, `w` W, `bv`, `ba`, `cs` (VE.Direct charge state), `err`, `yt` kWh today, `mx` peak W today, `ytot` |
| `tmp[3]` | inside, outside, fridge: `on`, `c`, `lo`, `hi` (today) |
| `tank[3]` | `on`, `p` % |
| `pump[2]` | `on`, `sn` sensed, `lk` dry-run lockout, `rt` run seconds |
| `inv` | `on`, `ac` (240 V present) |
| `ac` | `on`, `pre` preset index |
| `alerts[]` | `id`, `sev` (0 info, 1 warn, 2 alarm), `ack`, `since`, `val`, `msg` |
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
| `sensors` / `sensor_assign` | `addr`, `role` | DS18B20 role mapping |
| `remote_control` | `on` | **not accepted from the cloud** |
| `config_set` | `cfg:{…}` | partial config merge (see `HubConfig::toJson`) |
| `bmpro_devices`, `bmpro_log` | | Bluetooth devices seen / OdysseyLink log |
| `bmpro_connect` | `addr` | set + remember the OdysseyLink address |
| `bmpro_write` | `uuid`, `hex`, `resp` | raw write, for replaying captured Odyssey app commands |
| `reboot` | | |

When remote control is off, every command except the read-only ones is refused from MQTT.

Rule metric keys: `bat_v bat_soc bat_a load_a charge_a solar_w solar1_w solar2_w temp_in temp_out temp_fridge tank1 tank2 tank3`.
Actions: `none aircon_off inverter_off pumps_off`.

## Hub config (`GET/POST /api/config`)

`meta` (names, aircon presets, `rc`), `rules`, and the following:
- `bat`: `src` (2 BMPRO = default, 1 INA226 shunt), `cap` Ah, `eff`, `fullV`, `tailA`, `shunt` Ω, `chg`, `chgShunt`, `vCal`
- `tanks[]`: `empty`, `full` mV, `ch`, `en`
- `pumps[]`: `mode` (3 BMPRO over Bluetooth = default, 0 relay latch, 1 relay pulse, 2 disabled), `pulse` ms, `maxRun` min, `dryTank`, `dryPct`, `sense`
- `tankSrc`: 0 BMPRO (default), 1 ADS1115 taps, 2 none
- `bmproAddr`: OdysseyLink Bluetooth address
- `inv`: `mode`, `pulse`, `sense`
- `tempAddr[]`, `cloudInt` s, `buzzer`, `bmproExplorer` (verbose Bluetooth logging)
