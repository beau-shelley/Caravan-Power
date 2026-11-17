"""The X graphic from the side of the van, as rounded polygons in a 2000 x 1100
box (traced by eye from Beau's photo, perspective removed). Pieces are filled
with camo; the lime lines are drawn on top."""
import math

W, H = 2000, 1300
# Each piece: list of (x, y) corners. Gaps between pieces show the body colour.
PIECES = [
    # upper-left arm (long, to the top left), inner end squared off
    [(30, 90), (190, 0), (691, 460), (528, 641)],
    # lower-left arm (mirror of the upper one), inner end cut along the band
    [(528, 659), (691, 840), (190, 1300), (30, 1210)],
    # middle chevron band, pointed left, notched right
    [(560, 650), (740, 450), (990, 450), (890, 650), (990, 850), (740, 850)],
    # long bar to the right, pointed left end
    [(1030, 470), (1990, 520), (1990, 820), (1030, 830), (940, 650)],
]
RADIUS = 34
# Lime pinstripes: chevron line inside the middle band, and a line along the bar.
LINES = [
    [(945, 470), (845, 650), (945, 830)],
    [(1050, 770), (1950, 770)],
]
LINE_W = 11


def rounded(poly, r=RADIUS):
    """SVG path for a polygon with rounded corners (quadratic curves)."""
    n = len(poly)
    out = []
    for i in range(n):
        p0, p1, p2 = poly[i - 1], poly[i], poly[(i + 1) % n]
        def toward(a, b, d):
            dx, dy = b[0] - a[0], b[1] - a[1]
            L = math.hypot(dx, dy)
            d = min(d, L / 2.2)
            return (a[0] + dx / L * d, a[1] + dy / L * d)
        a = toward(p1, p0, r)
        b = toward(p1, p2, r)
        out.append(("M" if i == 0 else "L") + "%.0f %.0f" % a)
        out.append("Q%.0f %.0f %.0f %.0f" % (p1[0], p1[1], b[0], b[1]))
    return "".join(out) + "Z"


def pieces_d():
    return "".join(rounded(p) for p in PIECES)


def lines_d():
    return "".join("M" + "L".join("%d %d" % p for p in ln) for ln in LINES)
