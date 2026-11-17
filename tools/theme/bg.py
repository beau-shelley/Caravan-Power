"""Background art: body colour, the X graphic filled with camo, lime pinstripes."""
import camo, xglyph

# Colours sampled from Beau's close-ups, darkened where the screen needs it.
BODY = "#2b2e32"          # van body (gunmetal), darkened for a dark screen
CAMO = ["#121316", "#24262a", "#36393d", "#5b5f64"]  # base, dark, mid, light
LIME = "#dceb4b"          # the lime on the decals ("RV", pinstripes)
GREEN = "#90d130"         # the green dot in the logo


def camo_pattern(pid="camo", scale=1.0):
    L = camo.layers()
    T = camo.T
    p = [f'<pattern id="{pid}" patternUnits="userSpaceOnUse" width="{T*scale:.2f}" height="{T*scale:.2f}">',
         f'<rect width="{T*scale:.2f}" height="{T*scale:.2f}" fill="{CAMO[0]}"/>']
    for (f, t), c in zip(L, CAMO[1:]):
        p.append(f'<path fill="{c}" fill-rule="evenodd" d="{camo.svg_d(camo.trace(f, t), scale, 1)}"/>')
    p.append("</pattern>")
    return "".join(p)


WHITE = "#f2f3f1"


def svg(w, h, x0, y0, s, camo_scale=1.0, lines=True):
    """Whole background: camo everywhere, the van's X outlined in white with
    green pinstripes on top, glyph placed at (x0, y0) with scale s."""
    g = f'<g transform="translate({x0} {y0}) scale({s})">'
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">'
            f'<defs>{camo_pattern(scale=camo_scale)}</defs>'
            f'<rect width="{w}" height="{h}" fill="url(#camo)"/>'
            f'{g}<path fill="none" stroke="{WHITE}" stroke-opacity=".85" stroke-width="{6 / s:.1f}" d="{xglyph.pieces_d()}"/></g>'
            + (f'{g}<path fill="none" stroke="{GREEN}" stroke-width="{xglyph.LINE_W}" stroke-linejoin="miter" d="{xglyph.lines_d()}"/></g>' if lines else "")
            + "</svg>")
