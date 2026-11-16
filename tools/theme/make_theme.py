#!/usr/bin/env python3
"""Make the caravan theme textures from one source, so the touchscreen, the web
page and the Android app share the same camo and chequer plate.

  python3 tools/theme/make_theme.py

Writes:
  firmware/display/src/ui/tex_theme.c   camo + tread tiles for LVGL (RGB565)
  web/theme-camo.svg                    the camo tile (reference; inlined in web/index.html)
  android/app/src/main/java/com/caravan/monitor/ui/CamoPaths.kt
  tools/theme/out/*.png                 previews

The camo is a few layers of soft blobs (woodland style, stretched sideways like
the stripe on the van) on a seamless tile. Colours are close together on
purpose: it should read as texture, not as a pattern fighting the numbers.
Needs numpy + Pillow.
"""
import math
import os
import random

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(os.path.dirname(__file__), "out")
TILE = 240  # camo tile size in px (display) and SVG units

# Theme colours. Keep in sync with ui.h (col::), web/index.html (:root) and Colors.kt.
CAMO_BASE = 0x0E0F10
CAMO_LAYERS = [0x141617, 0x191C1C, 0x1F2321]  # last one leans olive, very slightly


def rgb(h):
    return ((h >> 16) & 255, (h >> 8) & 255, h & 255)


# ---------------------------------------------------------------- camo shapes
def blob(rng, cx, cy, rx, ry, rot, n=16):
    """Closed blob: an ellipse with a wobbly radius; returns control points."""
    k = [rng.uniform(0, 2 * math.pi) for _ in range(4)]
    a = [rng.uniform(0.18, 0.32), rng.uniform(0.10, 0.20), rng.uniform(0.06, 0.12), rng.uniform(0.04, 0.08)]
    pts = []
    for i in range(n):
        t = 2 * math.pi * i / n
        r = (1 + a[0] * math.sin(2 * t + k[0]) + a[1] * math.sin(3 * t + k[1])
             + a[2] * math.sin(5 * t + k[2]) + a[3] * math.sin(7 * t + k[3]))
        x, y = rx * r * math.cos(t), ry * r * math.sin(t)
        pts.append((cx + x * math.cos(rot) - y * math.sin(rot), cy + x * math.sin(rot) + y * math.cos(rot)))
    return pts


def catmull_to_bezier(pts):
    """Closed Catmull-Rom spline through pts -> list of cubic segments."""
    segs = []
    n = len(pts)
    for i in range(n):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[(i + 1) % n], pts[(i + 2) % n]
        c1 = (p1[0] + (p2[0] - p0[0]) / 6, p1[1] + (p2[1] - p0[1]) / 6)
        c2 = (p2[0] - (p3[0] - p1[0]) / 6, p2[1] - (p3[1] - p1[1]) / 6)
        segs.append((p1, c1, c2, p2))
    return segs


def make_layers(seed=7):
    rng = random.Random(seed)
    layers = []
    spec = [(12, 30, 58), (11, 22, 46), (10, 12, 30)]  # count, min, max half-width
    for count, lo, hi in spec:
        blobs = []
        for _ in range(count):
            rx = rng.uniform(lo, hi)
            ry = rx * rng.uniform(0.3, 0.55)
            rot = rng.uniform(-0.45, 0.25)  # mostly sideways, a little slant like the van stripe
            cx, cy = rng.uniform(0, TILE), rng.uniform(0, TILE)
            pts = blob(rng, cx, cy, rx, ry, rot)
            # Copies across the tile edges so the tile repeats seamlessly.
            for dx in (-TILE, 0, TILE):
                for dy in (-TILE, 0, TILE):
                    moved = [(x + dx, y + dy) for x, y in pts]
                    xs, ys = [p[0] for p in moved], [p[1] for p in moved]
                    if max(xs) < 0 or min(xs) > TILE or max(ys) < 0 or min(ys) > TILE:
                        continue
                    blobs.append(moved)
        layers.append(blobs)
    return layers


def svg_path(blobs):
    out = []
    for pts in blobs:
        segs = catmull_to_bezier(pts)
        d = "M%.0f %.0f" % segs[0][0]
        for _, c1, c2, p2 in segs:
            d += "C%.0f %.0f %.0f %.0f %.0f %.0f" % (c1 + c2 + p2)
        out.append(d + "Z")
    return "".join(out).replace(" -", "-")


def sample(segs, steps=10):
    pts = []
    for p1, c1, c2, p2 in segs:
        for s in range(steps):
            t = s / steps
            mt = 1 - t
            pts.append((mt**3 * p1[0] + 3 * mt * mt * t * c1[0] + 3 * mt * t * t * c2[0] + t**3 * p2[0],
                        mt**3 * p1[1] + 3 * mt * mt * t * c1[1] + 3 * mt * t * t * c2[1] + t**3 * p2[1]))
    return pts


def raster_camo(layers, size=TILE, ss=4):
    img = Image.new("RGB", (size * ss, size * ss), rgb(CAMO_BASE))
    d = ImageDraw.Draw(img)
    for col, blobs in zip(CAMO_LAYERS, layers):
        for pts in blobs:
            d.polygon([(x * ss * size / TILE, y * ss * size / TILE) for x, y in sample(catmull_to_bezier(pts))], fill=rgb(col))
    return img.resize((size, size), Image.LANCZOS)


# ---------------------------------------------------------------- chequer plate
TREAD = 24
TREAD_BASE = 0x0B0C0D
TREAD_RIDGE = 0x1B1D1F
TREAD_LIGHT = 0x2A2D30


def raster_tread(ss=8):
    """Diamond (chequer) plate: raised lozenges, alternately / and \\, lit from the top left."""
    s = TREAD * ss
    img = Image.new("RGB", (s, s), rgb(TREAD_BASE))
    d = ImageDraw.Draw(img)

    def lozenge(cx, cy, slash):
        L, W = 4.6, 1.5  # half length, half width (px)
        ang = -math.pi / 4 if slash else math.pi / 4
        ux, uy = math.cos(ang), math.sin(ang)
        vx, vy = -uy, ux
        pts = []
        for i in range(24):  # stadium shape
            t = 2 * math.pi * i / 24
            ex = L * math.copysign(abs(math.cos(t)) ** 0.35, math.cos(t))
            ey = W * math.sin(t)
            pts.append(((cx + ex * ux + ey * vx) * ss, (cy + ex * uy + ey * vy) * ss))
        return pts

    for (cx, cy, sl) in [(6, 6, True), (18, 18, True), (18, 6, False), (6, 18, False)]:
        for ox in (-TREAD, 0, TREAD):
            for oy in (-TREAD, 0, TREAD):
                # highlight offset up-left, then the ridge on top
                d.polygon([(x - 0.6 * ss, y - 0.6 * ss) for x, y in lozenge(cx + ox, cy + oy, sl)], fill=rgb(TREAD_LIGHT))
                d.polygon(lozenge(cx + ox, cy + oy, sl), fill=rgb(TREAD_RIDGE))
    return img.resize((TREAD, TREAD), Image.LANCZOS)


# ---------------------------------------------------------------- writers
def lv_c_array(name, img):
    a = np.asarray(img.convert("RGB"), dtype=np.uint16)
    v = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)
    data = v.astype("<u2").tobytes()
    w, h = img.size
    lines = []
    for i in range(0, len(data), 24):
        lines.append("  " + ",".join("0x%02x" % b for b in data[i:i + 24]) + ",")
    return f"""
static const uint8_t {name}_map[] = {{
{chr(10).join(lines)}
}};
const lv_img_dsc_t {name} = {{
  .header = {{ .cf = LV_IMG_CF_TRUE_COLOR, .always_zero = 0, .reserved = 0, .w = {w}, .h = {h} }},
  .data_size = {len(data)},
  .data = {name}_map,
}};
"""


def main():
    os.makedirs(OUT, exist_ok=True)
    layers = make_layers()
    camo = raster_camo(layers)
    tread = raster_tread()
    camo.save(os.path.join(OUT, "camo.png"))
    tread.save(os.path.join(OUT, "tread.png"))
    # Previews at a visible contrast too.
    big = Image.new("RGB", (TILE * 3, TILE * 2))
    for x in range(3):
        for y in range(2):
            big.paste(camo, (x * TILE, y * TILE))
    big.save(os.path.join(OUT, "camo-3x2.png"))
    tbig = Image.new("RGB", (TREAD * 8, TREAD * 4))
    for x in range(8):
        for y in range(4):
            tbig.paste(tread, (x * TREAD, y * TREAD))
    tbig.resize((TREAD * 16, TREAD * 8), Image.NEAREST).save(os.path.join(OUT, "tread-preview.png"))

    paths = [svg_path(b) for b in layers]

    with open(os.path.join(ROOT, "firmware/display/src/ui/tex_theme.c"), "w") as f:
        f.write("// Generated by tools/theme/make_theme.py - do not edit.\n")
        f.write("// Caravan theme textures: dark camo (screen background) and chequer plate (nav rail).\n")
        f.write('#include "lvgl.h"\n')
        f.write(lv_c_array("tex_camo", camo))
        f.write(lv_c_array("tex_tread", tread))

    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{TILE}" height="{TILE}" viewBox="0 0 {TILE} {TILE}">',
           f'<rect width="{TILE}" height="{TILE}" fill="#{CAMO_BASE:06x}"/>']
    for colr, p in zip(CAMO_LAYERS, paths):
        svg.append(f'<path fill="#{colr:06x}" d="{p}"/>')
    svg.append("</svg>")
    with open(os.path.join(ROOT, "web/theme-camo.svg"), "w") as f:
        f.write("\n".join(svg) + "\n")

    kt = ["// Generated by tools/theme/make_theme.py - do not edit.",
          "package com.caravan.monitor.ui", "",
          "/** Camo tile (size [CAMO_TILE]) as SVG path data, one string per layer, darkest first. */",
          f"const val CAMO_TILE = {TILE}f", "val CAMO_PATHS = listOf("]
    for p in paths:
        kt.append(f'    "{p}",')
    kt.append(")")
    with open(os.path.join(ROOT, "android/app/src/main/java/com/caravan/monitor/ui/CamoPaths.kt"), "w") as f:
        f.write("\n".join(kt) + "\n")
    print("paths:", [len(p) for p in paths])


if __name__ == "__main__":
    main()
