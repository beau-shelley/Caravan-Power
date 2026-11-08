# BMPRO integration

## Where things stand

| BMPRO part | What we do today | Future option |
|---|---|---|
| Odyssey Link 105 (battery monitor) | Leave it alone. Battery data comes from our own INA226 shunt | Read SoC, volts and amps over the CommLink once decoded, then drop the extra shunt |
| BMPRO 35A (charger / power manager) | Leave it alone. Optional second INA226 to measure its charge current | Charger state and mains present via the CommLink |
| CommLink (Bluetooth) | **Capture tool** in the hub logs everything it broadcasts | The decoded protocol becomes a data source |
| Trek 3 screen | Untouched; keeps working | n/a |
| Pump switching | Trek switching retained; we add a diode-OR'd feed | Switch pumps through the BMPRO itself (Trek stays in sync) |
| Tank senders | Read by the BMPRO as before; we tap the voltage | Tank % from the BMPRO directly |

BMPRO does not publish its CommLink / Trek protocol, and as of writing there is no public reverse-engineering of it. The hub therefore ships with a **BLE explorer** to make decoding it straightforward. Once the frames are understood, only one function needs filling in: `Bmpro::decode()` in [`firmware/hub/src/Bmpro.cpp`](../firmware/hub/src/Bmpro.cpp).

## Capturing the CommLink protocol

1. **Enable the explorer.** In the hub config (`/api/config`, or the `config_set` command), set:
   ```json
   { "bmproExplorer": true }
   ```
   Then reboot the hub. The BLE scan uses a low duty cycle, so it coexists with Wi-Fi.
2. **Find the CommLink.** Open `http://caravan.local/api/cmd` with the body `{"c":"bmpro_devices"}`, or watch the serial log. Advertisements from devices named BMPRO, CommLink or Trek are logged in full, including manufacturer data. *Some devices broadcast live values in their adverts; if so, no connection is needed.*
3. **Close the BMPRO phone app.** Most BLE peripherals accept only one connection at a time.
4. **Connect and dump.** From the dashboard → *Setup* → *BMPRO capture log*, enter the address and press **Connect**. The hub then:
   - lists every service and characteristic, with properties (R/W/N/I);
   - reads every readable characteristic;
   - subscribes to every notify/indicate characteristic and logs each frame as hex.
5. **Make known changes and watch the log** (*Refresh log*). For example:
   - Switch a pump on and off from the Trek: find the byte that toggles.
   - Turn on a known load (e.g. a 100 W light): find the current field. Try little-endian 16-bit, signed, in 0.1 A or 0.01 A units.
   - Compare the battery % shown on the Trek with byte values.
   - Plug in and unplug mains: find the charger state.
6. **Decode.** Write the decoder in `Bmpro::decode()`. There's a template in the comments. Then set `bat.src = 2` so the battery card uses BMPRO data.

If the CommLink only answers after the app writes a command (a request/response protocol), a phone BLE sniffer helps: on Android, enable *Bluetooth HCI snoop log* and open it in Wireshark. It shows exactly what the official app writes. Those writes can then be replayed from the hub.

Please share what you find. A decoded CommLink would help every BMPRO caravan owner.

## Why not tap the Trek wiring?

The Trek, JHub and Odyssey Link talk over a proprietary wired bus. Sniffing it needs the right transceiver (it's not known whether it's RS485, LIN or CAN-like), and a mistake can upset a system that controls 12 V distribution. Bluetooth is read-only and electrically isolated, so it is the safer place to start.
