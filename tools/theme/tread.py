"""Chequer (diamond) plate, like the skirt on the van: raised lozenges with
pointed ends, each one at right angles to its neighbours, lit from the top
left (bright edge up-left, shadow down-right). One 24 x 24 tile, used by the
touchscreen (raster), the web page (SVG) and the app (Compose paths)."""
import math

CELL = 24
HALF_LEN, HALF_W = 5.0, 1.9  # lozenge half length / half width
# (centre x, centre y, angle in degrees)
LOZENGES = [(6, 6, -45), (18, 18, -45), (18, 6, 45), (6, 18, 45)]
BASE, SHADOW, BODY, LIGHT, SHEEN = "#17181a", "#040405", "#33373b", "#80858c", "#464a50"


def lens_d(cx, cy, ang, dx=0.0, dy=0.0, k=1.0):
    """Pointed lens as an SVG path (two quadratic curves)."""
    a = math.radians(ang)
    ux, uy = math.cos(a), math.sin(a)
    vx, vy = -uy, ux
    L, W = HALF_LEN * k, HALF_W * 2 * k
    p0 = (cx - ux * L + dx, cy - uy * L + dy)
    p1 = (cx + ux * L + dx, cy + uy * L + dy)
    c1 = (cx + vx * W + dx, cy + vy * W + dy)
    c2 = (cx - vx * W + dx, cy - vy * W + dy)
    return "M%.2f %.2fQ%.2f %.2f %.2f %.2fQ%.2f %.2f %.2f %.2fZ" % (p0 + c1 + p1 + c2 + p0)


def layers():
    """(colour, path) list, back to front."""
    sh = "".join(lens_d(x, y, a, 0.8, 0.8) for x, y, a in LOZENGES)
    li = "".join(lens_d(x, y, a, -0.55, -0.55) for x, y, a in LOZENGES)
    bo = "".join(lens_d(x, y, a) for x, y, a in LOZENGES)
    sn = "".join(lens_d(x, y, a, -0.35, -0.35, 0.55) for x, y, a in LOZENGES)
    return [(SHADOW, sh), (LIGHT, li), (BODY, bo), (SHEEN, sn)]


def svg(size=CELL):
    s = size / CELL
    body = "".join(f'<path fill="{c}" d="{d}"/>' for c, d in layers())
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 {CELL} {CELL}">'
            f'<rect width="{CELL}" height="{CELL}" fill="{BASE}"/>{body}</svg>')
