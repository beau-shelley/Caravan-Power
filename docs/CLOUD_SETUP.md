# Cloud access & phone alerts

Starlink uses **CGNAT**: there's no public IP address, so you can't port-forward into the caravan. Instead, the hub makes an **outbound**, encrypted connection to an MQTT broker, and your phone or laptop connects to the same broker. Nothing in the van is exposed to the internet.

```
Hub ──TLS 8883──▶ MQTT broker ◀──WSS 8884── dashboard (web/index.html on phone/laptop)
Hub ──HTTPS────▶ ntfy.sh ──push──▶ ntfy app on your phone
```

The system works fully without any of this. The touchscreen and the local web page (`http://caravan.local`) only need the hub.

## 1. MQTT broker (HiveMQ Cloud, free tier)

1. Sign up at <https://www.hivemq.com/mqtt/cloud-broker/> and create a **Serverless (free)** cluster.
2. Note its **cluster URL** (e.g. `abc123.s1.eu.hivemq.cloud`). Its ports are:
   - **8883:** MQTT over TLS, used by the hub.
   - **8884:** MQTT over secure WebSockets, used by the dashboard at `wss://<cluster-url>:8884/mqtt`.
3. Under *Access management*, create two credentials:
   - `caravan-hub`: for the hub's `secrets.h`.
   - `caravan-dash`: for the dashboard. It's separate so you can revoke it without re-flashing the hub.
4. Put the hub's credentials in `firmware/hub/src/secrets.h`:
   ```c
   #define MQTT_HOST "abc123.s1.eu.hivemq.cloud"
   #define MQTT_PORT 8883
   #define MQTT_USER "caravan-hub"
   #define MQTT_PASS "…"
   ```

The hub trusts the ISRG Root X1 (Let's Encrypt) certificate, which HiveMQ Cloud and ntfy.sh use (see `root_ca.h`). If you use a different broker with a different CA, replace that certificate.

Any MQTT broker with TLS and WebSockets works: Mosquitto on a VPS, EMQX Cloud, or a Home Assistant add-on reached through Nabu Casa.

## 2. Remote dashboard

`web/index.html` is a single file with no build step. Host it either way:

- **GitHub Pages:** *Settings* → *Pages* → deploy from the branch, folder `/web`, then open `https://<you>.github.io/<repo>/`. The levelling page is at `…/level.html`. It needs HTTPS for Bluetooth, which GitHub Pages provides, and it's cached for offline use after the first visit.
- **Any static host,** or simply open the file on your laptop.

Open it, go to **Setup** → *Cloud connection*, and enter:

| Field | Value |
|---|---|
| Broker WebSocket URL | `wss://<cluster-url>:8884/mqtt` |
| Device id | `caravan` (the `DEVICE_ID` in `secrets.h`) |
| Username / password | the `caravan-dash` credential |

These are stored only in that browser's local storage. On a phone, use *Add to Home Screen* for an app-like icon.

On the caravan Wi-Fi, `http://caravan.local` serves the same dashboard straight from the hub. It updates live over a WebSocket and is protected by `WEB_USER` / `WEB_PASS`.

## 3. Phone push alerts (ntfy)

1. Install **ntfy** from the App Store or Google Play.
2. Invent a long, random topic name, e.g. `caravan-alerts-7Hq2xP9v`. Anyone who knows the topic can read the alerts, so treat it like a password.
3. In the app: **Subscribe to topic** → enter that name.
4. Put the same name in `secrets.h` as `NTFY_TOPIC`.

Priorities used:

| Alert | ntfy priority | Effect |
|---|---|---|
| Alarm | 5 (urgent) | Long vibration; can bypass Do Not Disturb if you allow it in the app |
| Warning | 4 (high) | |
| Info | 3 (default) | |
| Cleared | 2 (low) | Quiet confirmation |

Which rules notify is set per rule (the "Push" column on the dashboard).

For extra privacy, self-host ntfy or use an ntfy.sh account with an access token (`NTFY_TOKEN`).

## 4. Security notes

- **Encryption:** all traffic is TLS, and the hub verifies the broker's certificate.
- **Remote control switch:** controls (pumps, inverter, aircon) can be switched off for remote users. Use **Settings → Allow remote control** on the touchscreen; this switch can't be changed from the cloud.
- **Load shedding:** automatic actions only ever turn loads **off**.
- **Use your own broker:** don't use public test brokers such as `test.mosquitto.org`. Anyone could read and control your van.

## 5. Topics (for Home Assistant / Node-RED)

All topics are under `caravan/<DEVICE_ID>/`:

| Topic | Dir | Retained | Content |
|---|---|---|---|
| `state` | hub → | yes | full state JSON (every 15 s by default, and immediately on changes) |
| `meta` | hub → | yes | names, aircon presets, capacity |
| `rules` | hub → | yes | alert rules |
| `event` | hub → | no | `{"type":"raised"/"cleared","msg":…,"sev":…}` |
| `online` | hub → | yes | `1` / `0` (last will) |
| `cmd` | → hub | no | commands, see [PROTOCOL.md](PROTOCOL.md) |
| `resp` | hub → | no | command replies/errors (echoes your `rid`) |

## Data usage

A state message is ~800 bytes. At the default 15 s interval that's about **5 MB/day**. Increase `cloudIntervalSec` in the config if you're on a capped Starlink plan.
