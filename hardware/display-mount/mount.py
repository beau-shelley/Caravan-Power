#!/usr/bin/env python3
"""Parametric 3D-printed "backpack" that bolts the hub ESP32, the MPU-6050
level sensor, the RS485 module and the 5 V buck converter onto the back of
the Waveshare ESP32-S3-Touch-LCD-7B, using the four grey screw bosses on the
display's metal back plate.

Parts:
  tray.stl     sits on four spacers above the display's own circuit board and
               carries the hub parts. Print floor-down, no supports.
  lid.stl      closes the tray and screws to the wall. Print flat, rim up.
  spacer.stl   print 4. Tubes between the tray and the display's bosses.

Change the numbers in PARAMS and run:
    pip install manifold3d trimesh
    python mount.py        # writes the STLs and layout.svg next to this file
All sizes in mm. X is along the display's long side.
"""
import os
import numpy as np
from manifold3d import Manifold
import trimesh

PARAMS = dict(
    # --- display back: the four grey screw bosses ---
    boss_dx=126.2, boss_dy=65.65,  # hole centres. From Beau's photos ~124 x 64 (the ruler sat
                                   # above the plate, so slightly under); 126.2 x 65.65 is the
                                   # Raspberry Pi 7" pattern these modules copy. MEASURE.
    boss_screw_d=3.4,      # clearance for M3 (use 2.9 for M2.5)
    slot_extra=3.0,        # holes are slotted this much along X to absorb measuring error
    spacer_h=14.0,         # MEASURE: boss top to the top of the tallest part on the display
                           # board (PH2.0 plugs included), plus ~2 mm
    spacer_d=8.0,
    # --- tray ---
    tray=(140.0, 80.0),    # outside size
    floor_t=2.5,
    wall=2.0,
    wall_h=30.0,           # room for the ESP32 upside down with Dupont plugs on its pins
    lid_t=3.0,
    lid_post=8.0,
    lid_pilot_d=2.6,       # M3 self-tapping into the lid posts
    pass_slot=(14.0, 9.0), # wire pass-throughs to the display (5 V power + RS485 A/B leads)
    # --- hub ESP32 DevKit V1 (30-pin), mounted upside down, pins toward the lid ---
    esp_hole_dx=44.6, esp_hole_dy=23.7,  # measured from Beau's photos (44-45 x 23.7)
    esp_pcb=(51.0, 28.5),
    esp_standoff_h=6.0, esp_pilot_d=2.2,  # M2.5 self-tapping
    # --- MPU-6050 (GY-521) ---
    mpu_hole_dx=15.5, mpu_pcb=(21.0, 16.0),
    mpu_standoff_h=5.0, mpu_pilot_d=2.2,
    # --- zip-tie slot pairs ---
    max485=(44.0, 14.0), buck=(43.0, 21.0),
    tie_slot=(4.5, 2.2),
    cable_hole_d=12.0,     # 12 V in, through the lid
    wall_screw_d=4.5,
)

SEG = 48


def box(x, y, z, cx=0.0, cy=0.0, z0=0.0):
    return Manifold.cube([x, y, z]).translate([cx - x / 2, cy - y / 2, z0])


def cyl(d, h, cx=0.0, cy=0.0, z0=0.0, d2=None):
    return Manifold.cylinder(h, d / 2, (d2 if d2 is not None else d) / 2, SEG).translate([cx, cy, z0])


def slot(w, length, h, cx, cy, z0):
    """Slot along X: width w, overall length `length`."""
    r = w / 2
    a = cyl(w, h, cx - (length / 2 - r), cy, z0)
    b = cyl(w, h, cx + (length / 2 - r), cy, z0)
    return a + b + box(length - w, w, h, cx, cy, z0)


def bosses(p):
    return [(sx * p["boss_dx"] / 2, sy * p["boss_dy"] / 2) for sx in (-1, 1) for sy in (-1, 1)]


def inner(p):
    W, H = p["tray"]
    return W - 2 * p["wall"], H - 2 * p["wall"]


def lid_posts(p):
    iw, ih = inner(p)
    o = p["lid_post"] / 2
    return [(-(iw / 2 - o), -10.0), (iw / 2 - o, -10.0), (0.0, ih / 2 - o), (0.0, -(ih / 2 - o))]


def layout(p):
    iw, ih = inner(p)
    ew = p["esp_pcb"][0]
    return dict(
        esp=(iw / 2 - 3.0 - ew / 2, 14.0),  # USB-C end 3 mm from the +X wall
        max485=(-38.0, 25.0),
        buck=(-36.0, -22.0),
        mpu=(30.0, -22.0),
        pass1=(0.0, 12.0),
        pass2=(0.0, -12.0),
    )


def tray(p):
    W, H = p["tray"]
    iw, ih = inner(p)
    ft, wh = p["floor_t"], p["wall_h"]
    t = box(W, H, ft + wh) - box(iw, ih, wh + 1, z0=ft)
    L = layout(p)
    # screw slots down to the display bosses
    for cx, cy in bosses(p):
        t -= slot(p["boss_screw_d"], p["boss_screw_d"] + p["slot_extra"], ft + 1, cx, cy, -0.5)
    # wire pass-throughs to the display
    for k in ("pass1", "pass2"):
        t -= box(*p["pass_slot"], ft + 1, *L[k], -0.5)
    # lid posts
    for cx, cy in lid_posts(p):
        t += box(p["lid_post"], p["lid_post"], wh, cx, cy, ft)
        t -= cyl(p["lid_pilot_d"], 14, cx, cy, ft + wh - 14 + 0.01)
    # ESP32 standoffs (board upside down)
    ex, ey = L["esp"]
    for sx in (-1, 1):
        for sy in (-1, 1):
            cx, cy = ex + sx * p["esp_hole_dx"] / 2, ey + sy * p["esp_hole_dy"] / 2
            t += cyl(6.0, p["esp_standoff_h"], cx, cy, ft)
            t -= cyl(p["esp_pilot_d"], p["esp_standoff_h"], cx, cy, ft + 0.5)
    # USB-C notch in the +X wall: the socket sits between the board and the floor
    t -= box(p["wall"] * 3, 14.0, 12.0, W / 2, ey, ft + 0.5)
    # MPU-6050: two standoffs along one edge + a support bar under the other
    mx, my = L["mpu"]
    for sx in (-1, 1):
        cx, cy = mx + sx * p["mpu_hole_dx"] / 2, my + p["mpu_pcb"][1] / 2 - 2.5
        t += cyl(5.5, p["mpu_standoff_h"], cx, cy, ft)
        t -= cyl(p["mpu_pilot_d"], p["mpu_standoff_h"], cx, cy, ft + 0.5)
    t += box(p["mpu_pcb"][0] - 4, 3.0, p["mpu_standoff_h"], mx, my - p["mpu_pcb"][1] / 2 + 2.5, ft)
    # zip-tie slot pairs
    sw, sl = p["tie_slot"]
    for key in ("max485", "buck"):
        (cx, cy), (w, h) = L[key], p[key]
        for fx in (-0.3, 0.3):
            for sy in (-1, 1):
                t -= box(sw, sl, ft + 1, cx + fx * w, cy + sy * (h / 2 + 2.5), -0.5)
    # vents in the long walls
    for i in range(-4, 5):
        if abs(i * 13.0) < 9:
            continue
        for sy in (-1, 1):
            t -= box(3.0, p["wall"] * 3, wh - 12, i * 13.0, sy * H / 2, ft + 8)
    return t


def lid(p):
    W, H = p["tray"]
    iw, ih = inner(p)
    t = p["lid_t"]
    m = box(W, H, t)
    rim_w, rim_h = iw - 0.6, ih - 0.6
    rim = box(rim_w, rim_h, 3.0, z0=t) - box(rim_w - 3.0, rim_h - 3.0, 3.2, z0=t - 0.1)
    for cx, cy in lid_posts(p):
        rim -= box(p["lid_post"] + 1.2, p["lid_post"] + 1.2, 3.4, cx, cy, t - 0.2)
    m += rim
    # screws into the lid posts, countersunk on the outside (z=0 is the wall side)
    for cx, cy in lid_posts(p):
        m -= cyl(3.4, t + 1, cx, cy, -0.5)
        m -= cyl(6.6, 1.8, cx, cy, -0.01, d2=3.4)
    # wall screws
    for sx in (-1, 1):
        for sy in (-1, 1):
            cx, cy = sx * 45.0, sy * 22.0
            m -= cyl(p["wall_screw_d"], t + 1, cx, cy, -0.5)
            m -= cyl(p["wall_screw_d"] * 2, p["wall_screw_d"] / 2 + 0.1, cx, cy, -0.01, d2=p["wall_screw_d"])
    m -= cyl(p["cable_hole_d"], t + 1, 0, 0, -0.5)
    return m


def spacer(p):
    return cyl(p["spacer_d"], p["spacer_h"]) - cyl(p["boss_screw_d"], p["spacer_h"] + 1, z0=-0.5)


def layout_svg(p, path):
    """Top view of the tray (inside) with the parts' footprints."""
    W, H = p["tray"]
    L = layout(p)
    k = 5.0
    SW, SH = W * k + 40, H * k + 40

    def X(x):
        return SW / 2 + x * k

    def Y(y):
        return SH / 2 - y * k

    def rect(cx, cy, w, h, fill, label, sub=""):
        s = (f'<rect x="{X(cx - w / 2):.1f}" y="{Y(cy + h / 2):.1f}" width="{w * k:.1f}" height="{h * k:.1f}" '
             f'fill="{fill}" fill-opacity="0.35" stroke="#333"/>'
             f'<text x="{X(cx):.1f}" y="{Y(cy) + 5:.1f}" text-anchor="middle" font-size="15">{label}</text>')
        if sub:
            s += f'<text x="{X(cx):.1f}" y="{Y(cy) + 22:.1f}" text-anchor="middle" font-size="12">{sub}</text>'
        return s

    s = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {SW:.0f} {SH + 30:.0f}" font-family="Arial">',
         '<rect width="100%" height="100%" fill="#fff"/>',
         f'<rect x="{X(-W / 2):.1f}" y="{Y(H / 2):.1f}" width="{W * k:.1f}" height="{H * k:.1f}" fill="#f3e3d0" stroke="#333" stroke-width="2"/>']
    s.append(rect(*L["esp"], *p["esp_pcb"], "#3b82f6", "ESP32 hub", "upside down, USB-C →"))
    s.append(rect(*L["mpu"], *p["mpu_pcb"], "#22c55e", "MPU-6050"))
    s.append(rect(*L["max485"], *p["max485"], "#a855f7", "MAX485"))
    s.append(rect(*L["buck"], *p["buck"], "#ef4444", "12→5 V buck"))
    for kk in ("pass1", "pass2"):
        s.append(rect(*L[kk], *p["pass_slot"], "#ffffff", ""))
    s.append(f'<text x="{X(0)}" y="{Y(0) + 5}" text-anchor="middle" font-size="12">wires to display</text>')
    for cx, cy in bosses(p):
        s.append(f'<circle cx="{X(cx)}" cy="{Y(cy)}" r="{p["boss_screw_d"] * k / 2 + 2}" fill="#666"/>')
    for cx, cy in lid_posts(p):
        o = p["lid_post"] * k / 2
        s.append(f'<rect x="{X(cx) - o}" y="{Y(cy) - o}" width="{2 * o}" height="{2 * o}" fill="#bbb"/>')
    total = p["spacer_h"] + p["floor_t"] + p["wall_h"] + p["lid_t"]
    s.append(f'<text x="20" y="{SH + 18}" font-size="15">Tray, inside ({W:.0f} × {H:.0f} mm). Dark dots: screws to the display bosses '
             f'({p["boss_dx"]} × {p["boss_dy"]}). Light squares: lid posts. Adds {total:.0f} mm behind the display.</text>')
    s.append('</svg>')
    with open(path, "w") as f:
        f.write("\n".join(s))


def save(m, path):
    mesh = m.to_mesh()
    tm = trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3], faces=np.asarray(mesh.tri_verts))
    tm.export(path)
    return tm


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    for name, fn in (("tray", tray), ("lid", lid), ("spacer", spacer)):
        tm = save(fn(PARAMS), os.path.join(here, name + ".stl"))
        print(f"{name}.stl  {tm.extents.round(1)} mm  watertight={tm.is_watertight}")
    layout_svg(PARAMS, os.path.join(here, "layout.svg"))
