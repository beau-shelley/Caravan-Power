# Cloud access & phone alerts

Starlink uses **CGNAT**: there's no public IP address, so you can't port-forward into the caravan. Instead, the hub makes an **outbound**, encrypted connection to an MQTT broker, and your phone or laptop connects to the same broker. Nothing in the van is exposed to the internet.

```
Hub ──TLS 8883──▶ MQTT broker (EMQX) ◀──WSS 8084── phone app / web dashboard
Hub ──HTTPS────▶ ntfy.sh ──push──▶ ntfy app on your phone
```

The system works fully without any of this. The touchscreen, the phone app on the van Wi-Fi and the local web page (`http://caravan.local`) only need the hub.

## 1. MQTT broker (EMQX Cloud Serverless, free)

HiveMQ Cloud no longer has a free plan, so we use **EMQX Cloud Serverless**. Its free allowance (1 million session-minutes and 1 GB of traffic a month) covers the hub being online all month plus a phone checking in. Set the spending limit to **$0** so it can never charge you.

1. Sign up at <https://www.emqx.com/en/cloud> and create a **Serverless** deployment. Pick the region closest to you (Asia-Pacific for Australia) and set the **spend limit to $0**.
2. Open the deployment's **Overview**. Note the **Address** (e.g. `abc123.ala.asia-southeast1.emqxsl.com`). Its ports are:
   - **8883:** MQTT over TLS, used by the hub.
   - **8084:** MQTT over secure WebSockets (path `/mqtt`), used by the phone app and the web dashboard.
3. Go to **Access Control → Authentication → Add** and create two users. Make up a long random password for each, for example in PowerShell on the Surface:
   ```powershell
   -join ((48..57)+(65..90)+(97..122) | Get-Random -Count 24 | % {[char]$_})
   ```
   - `caravan-hub`: goes in the hub's `secrets.h`.
   - `caravan-dash`: goes in the phone app and the web dashboard. It's separate so you can change it without re-flashing the hub.

   Keep the passwords somewhere safe (a password manager). Don't paste them into chats or commit them.
4. Put the hub's details in `firmware/hub/src/secrets.h` (this file is git-ignored):
   ```c
   #define MQTT_HOST "abc123.ala.asia-southeast1.emqxsl.com"
   #define MQTT_PORT 8883
   #define MQTT_USER "caravan-hub"
   #define MQTT_PASS "…"
   ```
   Then flash the hub. Its serial log shows `[cloud] connected` within a few seconds of joining Wi-Fi. In the EMQX console, **Clients** then lists `caravan-hub`.
5. In the phone app: **Setup → Away from the van (cloud)**. Enter the Address (just the host name; the app adds `wss://…:8084/mqtt`), username `caravan-dash` and its password, then **Save**. Turn the phone's Wi-Fi off to check: Home shows an "Away from the van" strip and live numbers.

The app picks the connection by itself: on the van Wi-Fi it talks to the hub directly; anywhere else it goes through EMQX. It looks for the hub again every 30 s, so it switches back as soon as you're at the van.

The hub trusts DigiCert Global Root G2 (EMQX Cloud) and ISRG Root X1 (Let's Encrypt: ntfy.sh, HiveMQ Cloud), see `root_ca.h`. If you use a different broker with a different CA, replace that certificate.

Any MQTT broker with TLS and WebSockets works: Mosquitto on a VPS, HiveMQ, or a Home Assistant add-on. For a broker other than EMQX, give the app and web page a full `wss://host:port/path` URL.

## 2. Remote dashboard

`web/index.html` is a single file with no build step. Host it either way:

- **GitHub Pages:** *Settings* → *Pages* → deploy from the branch, folder `/web`, then open `https://<you>.github.io/<repo>/`. The levelling page is at `…/level.html`. It needs HTTPS for Bluetooth, which GitHub Pages provides, and it's cached for offline use after the first visit.
- **Any static host,** or simply open the file on your laptop.

Open it, go to **Setup** → *Cloud connection*, and enter:

| Field | Value |
|---|---|
| Broker address | the EMQX Address, e.g. `abc123.ala.asia-southeast1.emqxsl.com` (or a full `wss://` URL for another broker) |
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
