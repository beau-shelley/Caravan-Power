"""Seamless camo like the decals on the van: three independent layers of
organic, lobed blobs (dark grey, mid grey, light grey) on a near-black base.
Each layer is a thresholded, domain-warped periodic noise field, traced to
polygons so the same shapes can be drawn as SVG, Compose paths or raster."""
import numpy as np
from skimage import measure

T = 256  # tile size (units = px at 1x)


def periodic_noise(rng, n, kmin, kmax, aniso=1.0, power=1.6):
    """Sum of random Fourier modes with integer wave numbers -> tiles seamlessly."""
    f = np.zeros((n, n))
    y, x = np.mgrid[0:n, 0:n] / n * 2 * np.pi
    modes = 0
    for kx in range(-kmax, kmax + 1):
        for ky in range(0, kmax + 1):
            k = np.hypot(kx * aniso, ky)
            if k < kmin or k > kmax or (ky == 0 and kx <= 0):
                continue
            a = rng.normal() / k ** power
            f += a * np.cos(kx * x + ky * y + rng.uniform(0, 2 * np.pi))
            modes += 1
    return (f - f.mean()) / f.std()


def warped(rng, n):
    base = periodic_noise(rng, n, 1.5, 9)
    # Domain warp gives the lobes and "drips" real camo has.
    wx = periodic_noise(rng, n, 2, 7) * 7
    wy = periodic_noise(rng, n, 2, 7) * 7
    from scipy.ndimage import map_coordinates
    yy, xx = np.mgrid[0:n, 0:n].astype(float)
    w = map_coordinates(base, [yy + wy, xx + wx], order=3, mode="grid-wrap")
    detail = periodic_noise(rng, n, 9, 22, power=1.0) * 0.12
    return w + detail


def layers(seed=11, coverage=(0.46, 0.36, 0.17)):
    rng = np.random.default_rng(seed)
    out = []
    for cov in coverage:
        f = warped(rng, T)
        t = np.quantile(f, 1 - cov)
        out.append((f, t))
    return out


def trace(field, level, tol=0.7):
    """Polygons (list of Nx2 arrays, x/y in tile units) covering the tile seamlessly."""
    big = np.tile(field, (3, 3))
    polys = []
    for c in measure.find_contours(big, level):
        if not np.allclose(c[0], c[-1]):
            continue  # open contour at the 3x3 edge: far from the middle tile
        c = measure.approximate_polygon(c, tol)
        y, x = c[:, 0] - T, c[:, 1] - T
        if x.max() < -1 or x.min() > T + 1 or y.max() < -1 or y.min() > T + 1:
            continue
        polys.append(np.stack([x, y], 1))
    return polys


def svg_d(polys, scale=1.0, prec=0):
    fmt = "%.{0}f".format(prec)
    parts = []
    for p in polys:
        pts = p * scale
        s = "M" + " ".join((fmt + " " + fmt) % (a, b) for a, b in pts[:-1]) + "Z"
        parts.append(s)
    return "".join(parts).replace(" -", "-")
