#!/usr/bin/env python3
"""Parametric 3D-printed case that mounts the hub ESP32, the MPU-6050 level
sensor, the RS485 module and the 5 V buck directly behind the Waveshare
ESP32-S3-Touch-LCD-7B display.

Two parts:
  bezel.stl  front frame: holds the display glass by its edges and forms the
             walls of the case. Print face-down, no supports.
  back.stl   rear cover: carries the hub parts on standoffs / zip-tie slots
             and screws to the wall. Print flat, standoffs up.

Change the numbers in PARAMS (measure your parts!) and run:
    pip install manifold3d trimesh
    python mount.py            # writes bezel.stl and back.stl next to this file
All sizes in mm.
"""
import os
import numpy as np
from manifold3d import Manifold
import trimesh

PARAMS = dict(
    # --- display (Waveshare: touch version 192.96 x 110.76) ---
    glass_w=192.96, glass_h=110.76,
    fit=0.4,               # clearance around the glass, each side
    lip=3.0,               # how far the front lip overlaps the glass border
    lip_t=1.6,             # front lip thickness
    display_depth=14.0,    # MEASURE: glass front to the back of the tallest part on the display
    # --- case ---
    wall=2.4,
    hub_depth=30.0,        # room behind the display for the hub parts and their wires
    post=9.0,              # corner screw posts (square)
    pilot_d=2.6,           # pilot hole in posts for M3 self-tapping screws
    cover_t=3.0,
    # --- hub ESP32 (30-pin DevKit), mounted upside down so the pins face the display ---
    esp_hole_dx=47.0, esp_hole_dy=23.5,  # MEASURE: hole spacing, centre to centre
    esp_pcb=(51.5, 28.5),
    esp_standoff_h=6.0, esp_pilot_d=2.2, # M2.5 self-tapping
    # --- MPU-6050 (GY-521) ---
    mpu_hole_dx=15.5, mpu_pcb=(21.0, 16.0),
    mpu_standoff_h=5.0, mpu_pilot_d=2.2,
    # --- zip-tie slot pairs for the MAX485 module and the buck converter ---
    max485=(44.0, 14.0), buck=(43.0, 21.0),
    tie_slot=(4.5, 2.2),
    cable_hole_d=12.0,     # 12 V cable entry through the rear cover
    wall_screw_d=4.5,      # wall-mount screw clearance (countersunk)
)

SEG = 48


def box(x, y, z, cx=0.0, cy=0.0, z0=0.0):
    """Axis-aligned box centred on (cx, cy) in XY, from z0 to z0+z."""
    return Manifold.cube([x, y, z]).translate([cx - x / 2, cy - y / 2, z0])


def cyl(d, h, cx=0.0, cy=0.0, z0=0.0, d2=None):
    return Manifold.cylinder(h, d / 2, (d2 if d2 is not None else d) / 2, SEG).translate([cx, cy, z0])


def dims(p):
    iw = p["glass_w"] + 2 * p["fit"]
    ih = p["glass_h"] + 2 * p["fit"]
    ow = iw + 2 * p["wall"]
    oh = ih + 2 * p["wall"]
    depth = p["lip_t"] + p["display_depth"] + p["hub_depth"]
    return iw, ih, ow, oh, depth


def post_centres(p):
    iw, ih, *_ = dims(p)
    dx = iw / 2 - p["post"] / 2
    dy = ih / 2 - p["post"] / 2
    return [(sx * dx, sy * dy) for sx in (-1, 1) for sy in (-1, 1)]


# Layout of the parts on the rear cover (XY centre positions, cover coords).
def layout(p):
    return dict(
        esp=(68.0, -18.0),       # ESP32, USB-C end 3 mm from the right wall
        mpu=(-60.0, 25.0),       # level sensor, X arrow toward the van's front
        max485=(40.0, 28.0),
        buck=(-45.0, -22.0),
        cable=(0.0, 0.0),
    )


def bezel(p):
    iw, ih, ow, oh, depth = dims(p)
    shell = box(ow, oh, depth)
    # hollow above the lip
    shell -= box(iw, ih, depth, z0=p["lip_t"])
    # window in the lip
    shell -= box(p["glass_w"] - 2 * p["lip"], p["glass_h"] - 2 * p["lip"], p["lip_t"] + 0.2, z0=-0.1)
    # corner posts behind the display, with pilot holes from the back
    z_post = p["lip_t"] + p["display_depth"]
    h_post = depth - z_post
    for cx, cy in post_centres(p):
        shell += box(p["post"], p["post"], h_post, cx, cy, z_post)
        shell -= cyl(p["pilot_d"], 14, cx, cy, depth - 14 + 0.01)
    # ESP32 USB-C access slot in the right wall, level with the hub board
    L = layout(p)
    esp_x, esp_y = L["esp"]
    # The board sits upside down, so its USB-C socket is between the board and
    # the cover: notch the back edge of the wall (the cover closes it).
    shell -= box(p["wall"] * 3, 14.0, p["esp_standoff_h"] + 4.0, ow / 2, esp_y, depth - p["esp_standoff_h"] - 4.0)
    # vent slots in the top and bottom walls (hub zone only)
    for i in range(-5, 6):
        for sy in (-1, 1):
            shell -= box(3.0, p["wall"] * 3, 14.0, i * 14.0, sy * oh / 2, z_post + 6.0)
    return shell


def back(p):
    iw, ih, ow, oh, depth = dims(p)
    t = p["cover_t"]
    plate = box(ow, oh, t)
    # locating rim that slips inside the bezel walls
    rim_w, rim_h = iw - 0.6, ih - 0.6
    rim = box(rim_w, rim_h, 3.0, z0=t) - box(rim_w - 3.0, rim_h - 3.0, 3.2, z0=t - 0.1)
    for cx, cy in post_centres(p):   # notch the rim around the posts
        rim -= box(p["post"] + 1.2, p["post"] + 1.2, 3.4, cx, cy, t - 0.2)
    esp_y = layout(p)["esp"][1]
    rim -= box(6.0, 15.0, 3.4, rim_w / 2, esp_y, t - 0.2)   # USB-C notch
    plate += rim
    # case screws (countersunk from the outside, z=0 is the wall side)
    for cx, cy in post_centres(p):
        plate -= cyl(3.4, t + 1, cx, cy, -0.5)
        plate -= cyl(6.6, 1.8, cx, cy, -0.01, d2=3.4)
    # wall-mount screws
    for sx in (-1, 1):
        for sy in (-1, 1):
            cx, cy = sx * (ow / 2 - 30), sy * (oh / 2 - 20)
            plate -= cyl(p["wall_screw_d"], t + 1, cx, cy, -0.5)
            plate -= cyl(p["wall_screw_d"] * 2, p["wall_screw_d"] / 2 + 0.1, cx, cy, -0.01, d2=p["wall_screw_d"])
    L = layout(p)
    # cable entry
    plate -= cyl(p["cable_hole_d"], t + 1, *L["cable"], -0.5)
    # ESP32 standoffs
    ex, ey = L["esp"]
    for sx in (-1, 1):
        for sy in (-1, 1):
            cx, cy = ex + sx * p["esp_hole_dx"] / 2, ey + sy * p["esp_hole_dy"] / 2
            plate += cyl(6.0, p["esp_standoff_h"], cx, cy, t)
            plate -= cyl(p["esp_pilot_d"], p["esp_standoff_h"] + 0.1, cx, cy, t + 0.5)
    # MPU-6050: two standoffs along one edge plus a support pad under the other
    mx, my = L["mpu"]
    for sx in (-1, 1):
        cx, cy = mx + sx * p["mpu_hole_dx"] / 2, my + p["mpu_pcb"][1] / 2 - 2.5
        plate += cyl(5.5, p["mpu_standoff_h"], cx, cy, t)
        plate -= cyl(p["mpu_pilot_d"], p["mpu_standoff_h"] + 0.1, cx, cy, t + 0.5)
    plate += box(p["mpu_pcb"][0] - 4, 3.0, p["mpu_standoff_h"], mx, my - p["mpu_pcb"][1] / 2 + 2.5, t)
    # zip-tie slot pairs either side of the MAX485 and the buck
    sw, sl = p["tie_slot"]
    for key in ("max485", "buck"):
        (cx, cy), (w, h) = L[key], p[key]
        for fx in (-0.3, 0.3):
            for sy in (-1, 1):
                plate -= box(sw, sl, t + 1, cx + fx * w, cy + sy * (h / 2 + 2.5), -0.5)
    return plate


def save(m, path):
    mesh = m.to_mesh()
    tm = trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3], faces=np.asarray(mesh.tri_verts))
    tm.export(path)
    return tm


def layout_svg(p, path):
    """Top view of the rear cover (inside face) with the parts' footprints."""
    iw, ih, ow, oh, depth = dims(p)
    L = layout(p)
    k = 4.0  # px per mm
    W, H = ow * k + 40, oh * k + 40

    def X(x):
        return W / 2 + x * k

    def Y(y):
        return H / 2 - y * k

    def rect(cx, cy, w, h, fill, label):
        return (f'<rect x="{X(cx - w / 2):.1f}" y="{Y(cy + h / 2):.1f}" width="{w * k:.1f}" height="{h * k:.1f}" '
                f'fill="{fill}" fill-opacity="0.35" stroke="#333"/>'
                f'<text x="{X(cx):.1f}" y="{Y(cy) + 5:.1f}" text-anchor="middle" font-size="15">{label}</text>')

    s = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W:.0f} {H + 30:.0f}" font-family="Arial">',
         f'<rect width="100%" height="100%" fill="#fff"/>',
         f'<rect x="{X(-ow / 2):.1f}" y="{Y(oh / 2):.1f}" width="{ow * k:.1f}" height="{oh * k:.1f}" fill="#f3e3d0" stroke="#333" stroke-width="2"/>']
    s.append(rect(*L["esp"], *p["esp_pcb"], "#3b82f6", "ESP32 hub"))
    s.append(rect(*L["mpu"], *p["mpu_pcb"], "#22c55e", "MPU-6050"))
    s.append(rect(*L["max485"], *p["max485"], "#a855f7", "MAX485"))
    s.append(rect(*L["buck"], *p["buck"], "#ef4444", "12→5 V buck"))
    cx, cy = L["cable"]
    s.append(f'<circle cx="{X(cx)}" cy="{Y(cy)}" r="{p["cable_hole_d"] / 2 * k}" fill="#fff" stroke="#333"/>'
             f'<text x="{X(cx)}" y="{Y(cy) + p["cable_hole_d"] * k / 2 + 18}" text-anchor="middle" font-size="14">12 V in</text>')
    for cx, cy in post_centres(p):
        s.append(f'<rect x="{X(cx) - p["post"] * k / 2}" y="{Y(cy) - p["post"] * k / 2}" width="{p["post"] * k}" height="{p["post"] * k}" fill="#999"/>')
    ex, ey = L["esp"]
    s.append(f'<text x="{X(ex)}" y="{Y(ey) + 22}" text-anchor="middle" font-size="12">upside down, USB-C →</text>')
    s.append(f'<text x="20" y="{H + 18}" font-size="15">Rear cover, inside face ({ow:.0f} × {oh:.0f} mm). Grey squares: case screw posts. Case depth {depth + p["cover_t"]:.0f} mm.</text>')
    s.append('</svg>')
    with open(path, "w") as f:
        f.write("\n".join(s))


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    for name, fn in (("bezel", bezel), ("back", back)):
        tm = save(fn(PARAMS), os.path.join(here, name + ".stl"))
        print(f"{name}.stl  {tm.extents.round(1)} mm  watertight={tm.is_watertight}")
    layout_svg(PARAMS, os.path.join(here, "layout.svg"))
