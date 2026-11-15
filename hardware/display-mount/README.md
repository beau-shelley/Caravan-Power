# Display + hub case (3D print)

A two-part case that mounts the hub behind the Waveshare ESP32-S3-Touch-LCD-7B. The display, hub ESP32, MPU-6050 level sensor, MAX485 module and 12→5 V buck converter all end up in one box, with a single 12 V cable going in.

![Rear cover layout](layout.svg)

| File | What | Print |
|---|---|---|
| `bezel.stl` | Front frame. It holds the display glass by its edges (3 mm lip) and forms the case walls. | Face down, no supports. PETG or ASA (a caravan gets hot). |
| `back.stl` | Rear cover. It carries the parts on standoffs and zip-tie slots, and screws to the wall. | Flat, standoffs up. |

**⚠️ Draft: check the sizes before printing.** Only the glass size (192.96 × 110.76 mm) is from Waveshare's spec. These values in `PARAMS` in `mount.py` are typical sizes I haven't measured:
- `display_depth`: glass front to the back of the tallest part on the display (default 14 mm).
- `esp_hole_dx`, `esp_hole_dy`: the ESP32 board's mounting-hole spacing (default 47 × 23.5 mm).
- `mpu_hole_dx`: the GY-521's hole spacing (default 15.5 mm).

To change them, edit `PARAMS`, then run:

```sh
pip install manifold3d trimesh
python mount.py      # rewrites bezel.stl, back.stl and layout.svg
```

## Assembly

1. **Display:** drop it face down into the bezel. The four corner posts stop just behind it; add a strip of foam tape on each post if it rattles.
2. **Cover to the wall:** screw the rear cover to the wall over the old Trek 3 spot with four countersunk screws. Feed the 12 V cable through the centre hole.
3. **Mount the parts on the cover:**
   - **ESP32:** mount it **upside down** (pins pointing at the display) on its four standoffs, using M2.5 self-tapping screws. Its USB-C port lines up with the notch in the bezel's right wall, so you can flash it without opening the case.
   - **MPU-6050:** mount it on its two standoffs with M2.5 screws. It must be solid; any angle is fine, because *Set level* calibrates it out.
   - **MAX485 and buck converter:** hold them down with zip ties through the slot pairs. A pad of double-sided foam tape underneath stops them rattling.
4. **Wire it up** per [docs/WIRING.md](../../docs/WIRING.md#hub-on-a-classic-esp32-mounted-behind-the-display). Use a short right-angle USB-C lead from the buck to the display.
5. **Close it:** fit the bezel over the cover (the cover's rim locates it) and fix it with four M3 × 12 self-tapping screws into the corner posts.

The case is about 199 × 116 mm and 49 mm deep. The top and bottom walls have vent slots.
