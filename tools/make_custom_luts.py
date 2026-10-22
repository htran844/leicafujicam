#!/usr/bin/env python3
"""Generate film-look .plut LUTs for leicafujicam (PLUT v1, size 33, UINT16).

Layout replicates the app's own LutConverter: header int32 LE
[MAGIC 'PLUT', version=1, size=33, dataType=1], then nodes in
b->g->r loop order (r fastest), each node R,G,B uint16 LE.
Color work in sRGB space: WB gains -> per-channel PCHIP tone curves ->
saturation/vibrance -> hue-selective tweaks (HSV masks) -> split toning.
"""
import struct
import numpy as np
from scipy.interpolate import PchipInterpolator

SIZE = 33
OUT_DIR = "/home/hatch/workspace/leicafujicam/app/src/main/assets/luts"

# full node grid, r fastest (matches b,g,r loop order)
axis = np.linspace(0.0, 1.0, SIZE)
bb, gg, rr = np.meshgrid(axis, axis, axis, indexing="ij")
BASE = np.stack([rr.ravel(), gg.ravel(), bb.ravel()], axis=1)  # (N,3) RGB inputs


def luma(rgb):
    return rgb @ np.array([0.2126, 0.7152, 0.0722])


def curve_from_points(points):
    xs = np.array([p[0] for p in points])
    ys = np.array([p[1] for p in points])
    return PchipInterpolator(xs, ys, extrapolate=True)


def apply_curves(rgb, curves):
    out = np.empty_like(rgb)
    for c in range(3):
        out[:, c] = np.clip(curves[c](rgb[:, c]), 0.0, 1.0)
    return out


def saturate(rgb, amount):
    """amount scalar or per-node array; 1.0 = unchanged."""
    l = luma(rgb)[:, None]
    return np.clip(l + (rgb - l) * amount, 0.0, 1.0)


def rgb_to_hsv(rgb):
    r, g, b = rgb[:, 0], rgb[:, 1], rgb[:, 2]
    mx = rgb.max(axis=1); mn = rgb.min(axis=1); d = mx - mn
    h = np.zeros_like(mx)
    nz = d > 1e-9
    idx = nz & (mx == r); h[idx] = ((g - b)[idx] / d[idx]) % 6
    idx = nz & (mx == g); h[idx] = (b - r)[idx] / d[idx] + 2
    idx = nz & (mx == b); h[idx] = (r - g)[idx] / d[idx] + 4
    h = (h * 60.0) % 360.0
    s = np.where(mx > 1e-9, d / np.maximum(mx, 1e-9), 0.0)
    return h, s, mx


def hsv_to_rgb(h, s, v):
    h = (h % 360.0) / 60.0
    c = v * s
    x = c * (1 - np.abs(h % 2 - 1))
    z = np.zeros_like(c)
    m = v - c
    out = np.empty((len(h), 3))
    seg = h.astype(int) % 6
    for i, (cr, cg, cb) in enumerate([(c, x, z), (x, c, z), (z, c, x),
                                      (z, x, c), (x, z, c), (c, z, x)]):
        msk = seg == i
        out[msk] = np.stack([cr, cg, cb], axis=1)[msk]
    return out + m[:, None]


def hue_band(h, center, width):
    """smooth 0..1 mask around hue center (degrees, circular)."""
    d = np.abs((h - center + 180.0) % 360.0 - 180.0)
    return np.clip(1.0 - d / width, 0.0, 1.0) ** 2


def hue_tweaks(rgb, rules):
    """rules: list of (center_deg, width_deg, hue_shift_deg, sat_scale)"""
    h, s, v = rgb_to_hsv(rgb)
    for center, width, shift, sscale in rules:
        m = hue_band(h, center, width)
        h = h + shift * m
        s = np.clip(s * (1.0 + (sscale - 1.0) * m), 0.0, 1.0)
    return hsv_to_rgb(h, s, v)


def split_tone(rgb, shadow_tint, highlight_tint, strength=1.0):
    l = luma(rgb)
    sw = ((1.0 - l) ** 2)[:, None] * strength
    hw = (l ** 2)[:, None] * strength
    out = rgb + np.array(shadow_tint)[None, :] * sw + np.array(highlight_tint)[None, :] * hw
    return np.clip(out, 0.0, 1.0)


def wb(rgb, gains):
    return np.clip(rgb * np.array(gains)[None, :], 0.0, 1.0)


def build(spec):
    rgb = wb(BASE.copy(), spec.get("wb", (1.0, 1.0, 1.0)))
    curves = spec["curves"]
    if isinstance(curves, list) and curves and not isinstance(curves[0], tuple):
        curves = [curve_from_points(p) for p in curves]
    else:
        c = curve_from_points(curves)
        curves = [c, c, c]
    rgb = apply_curves(rgb, curves)
    if "sat" in spec:
        rgb = saturate(rgb, spec["sat"])
    if "hue_rules" in spec:
        rgb = hue_tweaks(rgb, spec["hue_rules"])
    if "split" in spec:
        st, ht, s = spec["split"]
        rgb = split_tone(rgb, st, ht, s)
    return np.clip(rgb, 0.0, 1.0)


SOFT_FILM = [(0, 0.010), (0.25, 0.255), (0.5, 0.505), (0.75, 0.755), (1, 0.988)]

SPECS = {
    # Kodak Portra 400: warm-neutral, soft low contrast, pastel, protected skin,
    # muted greens/blues, gentle highlight roll-off.
    "kodak_portra_400": {
        "wb": (1.045, 1.0, 0.95),
        "curves": SOFT_FILM,
        "sat": 0.93,
        "hue_rules": [
            (120, 70, 6, 0.90),    # greens: slightly yellow, calmer
            (210, 70, 0, 0.88),    # blues: quieter
            (25, 45, 0, 1.05),     # skin/orange: gentle richness
        ],
        "split": ((-0.004, -0.002, -0.006), (0.012, 0.005, -0.010), 0.8),
    },
    # Kodak Gold 200: golden warmth, amber lifted shadows, sunny saturation.
    "kodak_gold_200": {
        "wb": (1.09, 1.0, 0.875),
        "curves": [
            [(0, 0.038), (0.25, 0.270), (0.5, 0.515), (0.75, 0.760), (1, 0.985)],  # R
            [(0, 0.028), (0.25, 0.260), (0.5, 0.505), (0.75, 0.750), (1, 0.980)],  # G
            [(0, 0.014), (0.25, 0.240), (0.5, 0.490), (0.75, 0.735), (1, 0.970)],  # B
        ],
        "sat": 1.13,
        "hue_rules": [
            (50, 45, 0, 1.18),     # yellows glow
            (25, 45, 0, 1.10),     # oranges warm
            (120, 70, 4, 0.95),    # greens slightly golden
        ],
        "split": ((0.016, 0.006, -0.012), (0.014, 0.006, -0.010), 0.9),
    },
    # Teal & Orange: teal shadows, warm highlights, punchy contrast, teal-shifted blues.
    "teal_orange": {
        "wb": (1.0, 1.0, 1.0),
        "curves": [(0, 0.004), (0.25, 0.205), (0.5, 0.50), (0.75, 0.795), (1, 0.996)],
        "sat": 1.16,
        "hue_rules": [
            (215, 60, -18, 1.05),  # blues -> teal
            (160, 60, -8, 1.05),   # cyans/greens deepen teal
            (25, 40, 2, 1.08),     # skin/orange stays warm & rich
        ],
        "split": ((-0.020, 0.026, 0.040), (0.026, 0.010, -0.020), 1.0),
    },
    # Tomosaki (photono_gen): vivid Japanese summer "seishun" look — deep cobalt
    # sky blue, lush saturated greens, clean neutral-cool whites, warm natural
    # skin, punchy contrast with true blacks. Tuned from measured stats of the
    # photographer's published photos (blue hue ~206 sat .64, green hue ~110
    # sat .55, skin hue ~24 sat .41, highlights near-neutral with a cool lean).
    "tomosaki": {
        "wb": (0.99, 1.0, 1.03),
        "curves": [(0, 0.0), (0.25, 0.222), (0.5, 0.52), (0.75, 0.802), (1, 0.996)],
        "sat": 1.22,
        "hue_rules": [
            (212, 58, -5, 1.38),   # sky blue -> deep cobalt azure
            (120, 65, -7, 1.32),   # greens -> lush emerald (~110 measured)
            (185, 40, -4, 1.15),   # cyan/teal accents rich
            (25, 38, 2, 1.06),     # skin/warm tones stay natural & warm
            (0, 35, 0, 1.12),      # reds pop gently
        ],
        "split": ((-0.004, 0.006, 0.016), (0.002, 0.004, 0.008), 0.7),
    },
}


def write_plut(path, rgb):
    data = np.clip(rgb, 0.0, 1.0)
    q = np.round(data * 65535.0).astype("<u2")
    with open(path, "wb") as f:
        f.write(struct.pack("<4i", 0x54554C50, 1, SIZE, 1))
        f.write(q.tobytes(order="C"))


if __name__ == "__main__":
    import os
    for name, spec in SPECS.items():
        out = build(spec)
        p = os.path.join(OUT_DIR, f"{name}.plut")
        write_plut(p, out)
        print(name, "->", p, os.path.getsize(p), "bytes")
