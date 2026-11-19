"""Background art: body colour, the X graphic filled with camo, lime pinstripes."""
import camo, xglyph

# Colours sampled from Beau's close-ups, darkened where the screen needs it.
BODY = "#2b2e32"          # van body (gunmetal), darkened for a dark screen
CAMO = ["#16181b", "#1c1f22", "#24272a", "#2e3135"]  # base, dark, mid, light: kept subtle
PLAIN = "#111214"         # plain background around the X
LIME = "#dceb4b"          # the lime on the decals ("RV", pinstripes)
GREEN = "#9be03a"         # highlight green (the decals), as on the screens


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


def svg(w, h, x0, y0, s, camo_scale=1.0, lines=True, strip=0, x=True):
    """Plain dark background; the van's X filled with subtle camo, outlined in
    white, with green pinstripes. strip > 0 also puts camo behind a header strip.
    x=False leaves the X out (plain background, camo header strip only)."""
    g = f'<g transform="translate({x0} {y0}) scale({s})">'
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">'
            f'<defs>{camo_pattern(scale=camo_scale)}{camo_pattern("camoX", camo_scale / s)}</defs>'
            f'<rect width="{w}" height="{h}" fill="{PLAIN}"/>'
            + (f'<rect width="{w}" height="{strip}" fill="url(#camo)"/>' if strip else "")
            + ("" if not x else
               f'<path fill="url(#camoX)" transform="translate({x0} {y0}) scale({s})" d="{xglyph.pieces_d()}"/>'
            f'{g}<path fill="none" stroke="{WHITE}" stroke-opacity=".8" stroke-width="{4 / s:.1f}" d="{xglyph.pieces_d()}"/></g>'
            + (f'{g}<path fill="none" stroke="{GREEN}" stroke-width="{xglyph.LINE_W}" stroke-linejoin="miter" d="{xglyph.lines_d()}"/></g>' if lines else ""))
            + "</svg>")
