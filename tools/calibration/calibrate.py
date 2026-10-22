#!/usr/bin/env python3
"""Per-device color calibration for leicafujicam (offline device LUT packs).

Pipeline:
  1. Take a photo of an X-Rite ColorChecker 24 chart shot through the app
     with the LUT set to "None" (see README.md for shooting instructions).
  2. Extract the mean sRGB of the 24 patches (OpenCV mcc auto-detection, or
     a manual --corners quad sampler as fallback).
  3. Fit a polynomial correction in LINEAR RGB (measured -> reference).
  4. Bake the correction into a 33^3 LUT and compose it with each generic
     style LUT:  device_lut(node) = correction(style(node)).
  5. Emit an offline "device pack" under <assets-luts>/devices/<device-key>/
     plus a report.json with per-patch Delta E 2000 before/after.

.plut format (identical to tools/make_custom_luts.py / the app's LutParser):
header int32 LE [magic 0x54554C50 'PLUT', version, size, dataType], then
nodes in b->g->r loop order (r fastest), each node R,G,B components.
Writer emits version=1, size=33, dataType=1 (uint16) -> 215,638-byte files.
The reader additionally accepts dataType=0 (uint8) and versions 2/3
(extra header ints), because some bundled generic LUTs use them.
"""
import argparse
import copy
import json
import os
import struct
import sys
import tempfile

import numpy as np

PLUT_MAGIC = 0x54554C50  # 'PLUT' little-endian
LUT_SIZE = 33
PLUT_BYTES_UINT16_33 = 16 + LUT_SIZE ** 3 * 3 * 2  # 215,638

DEFAULT_STYLES = ("leica,cc,Hasselblad,tomosaki,kodak_portra_400,"
                  "kodak_gold_200,nc,teal_orange")
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_ASSETS = os.path.normpath(
    os.path.join(SCRIPT_DIR, "..", "..", "app", "src", "main", "assets", "luts"))

# ---------------------------------------------------------------------------
# X-Rite ColorChecker 24 reference values.
# Source: BabelColor, "The ColorChecker (since 1976)" / "RGB coordinates of
# the Macbeth ColorChecker" — published average sRGB (D65) coordinates of
# production ColorChecker charts, row-major starting at Dark Skin.
# ---------------------------------------------------------------------------
PATCH_NAMES = [
    "Dark Skin", "Light Skin", "Blue Sky", "Foliage", "Blue Flower",
    "Bluish Green", "Orange", "Purplish Blue", "Moderate Red", "Purple",
    "Yellow Green", "Orange Yellow", "Blue", "Green", "Red", "Yellow",
    "Magenta", "Cyan", "White 9.5", "Neutral 8", "Neutral 6.5",
    "Neutral 5", "Neutral 3.5", "Black 2",
]
REFERENCE_SRGB_255 = np.array([
    [115,  82,  68],   # 1  Dark Skin
    [194, 150, 130],   # 2  Light Skin
    [ 98, 122, 157],   # 3  Blue Sky
    [ 87, 108,  67],   # 4  Foliage
    [133, 128, 177],   # 5  Blue Flower
    [103, 189, 170],   # 6  Bluish Green
    [214, 126,  44],   # 7  Orange
    [ 80,  91, 166],   # 8  Purplish Blue
    [193,  90,  99],   # 9  Moderate Red
    [ 94,  60, 108],   # 10 Purple
    [157, 188,  64],   # 11 Yellow Green
    [224, 163,  46],   # 12 Orange Yellow
    [ 56,  61, 150],   # 13 Blue
    [ 70, 148,  73],   # 14 Green
    [175,  54,  60],   # 15 Red
    [231, 199,  31],   # 16 Yellow
    [187,  86, 149],   # 17 Magenta
    [  8, 133, 161],   # 18 Cyan
    [243, 243, 242],   # 19 White 9.5
    [200, 200, 200],   # 20 Neutral 8
    [160, 160, 160],   # 21 Neutral 6.5
    [122, 122, 121],   # 22 Neutral 5
    [ 85,  85,  85],   # 23 Neutral 3.5
    [ 52,  52,  52],   # 24 Black 2
], dtype=np.float64)
REFERENCE_SRGB = REFERENCE_SRGB_255 / 255.0

GRID_AXIS = np.linspace(0.0, 1.0, LUT_SIZE)


# ---------------------------------------------------------------------------
# .plut I/O (writer mirrors tools/make_custom_luts.py exactly)
# ---------------------------------------------------------------------------
def write_plut(path, rgb):
    """rgb: (33^3, 3) float 0..1 in b->g->r order -> PLUT v1/33/uint16 file."""
    data = np.clip(np.asarray(rgb, dtype=np.float64), 0.0, 1.0)
    q = np.round(data * 65535.0).astype("<u2")
    with open(path, "wb") as f:
        f.write(struct.pack("<4i", PLUT_MAGIC, 1, LUT_SIZE, 1))
        f.write(q.tobytes(order="C"))


def read_plut(path):
    """Parse a .plut file -> (size, lut) with lut[b, g, r, channel] float 0..1."""
    raw = open(path, "rb").read()
    if len(raw) < 16:
        raise ValueError(f"{path}: file too short for a PLUT header")
    magic, version, size, data_type = struct.unpack_from("<4i", raw, 0)
    if magic != PLUT_MAGIC:
        raise ValueError(f"{path}: bad magic 0x{magic:08x} (not a .plut file)")
    offset = 16
    if version >= 2:
        offset += 4  # input transfer-curve id
    if version >= 3:
        offset += 4  # input color-space ordinal
    count = size * size * size * 3
    if data_type == 1:
        vals = np.frombuffer(raw, dtype="<u2", count=count, offset=offset)
        lut = vals.astype(np.float64) / 65535.0
    elif data_type == 0:
        vals = np.frombuffer(raw, dtype=np.uint8, count=count, offset=offset)
        lut = vals.astype(np.float64) / 255.0
    else:
        raise ValueError(f"{path}: unsupported PLUT data type {data_type}")
    if lut.size != count:
        raise ValueError(f"{path}: truncated data ({lut.size} of {count} values)")
    return size, lut.reshape(size, size, size, 3)


def grid_nodes():
    """(33^3, 3) sRGB grid points in b->g->r order (r fastest)."""
    bb, gg, rr = np.meshgrid(GRID_AXIS, GRID_AXIS, GRID_AXIS, indexing="ij")
    return np.stack([rr.ravel(), gg.ravel(), bb.ravel()], axis=1)


def sample_lut(lut, points):
    """Trilinear interpolation. lut: (S,S,S,3) [b,g,r,ch]; points: (N,3) RGB 0..1."""
    size = lut.shape[0]
    p = np.clip(np.asarray(points, dtype=np.float64), 0.0, 1.0) * (size - 1)
    lo = np.floor(p).astype(np.int64)
    hi = np.minimum(lo + 1, size - 1)
    frac = p - lo
    out = np.zeros((len(p), 3), dtype=np.float64)
    # axes of p are (r, g, b); lut axes are (b, g, r)
    for dr in (0, 1):
        for dg in (0, 1):
            for db in (0, 1):
                idx_r = hi[:, 0] if dr else lo[:, 0]
                idx_g = hi[:, 1] if dg else lo[:, 1]
                idx_b = hi[:, 2] if db else lo[:, 2]
                w = ((frac[:, 0] if dr else 1.0 - frac[:, 0])
                     * (frac[:, 1] if dg else 1.0 - frac[:, 1])
                     * (frac[:, 2] if db else 1.0 - frac[:, 2]))
                out += lut[idx_b, idx_g, idx_r] * w[:, None]
    return out


# ---------------------------------------------------------------------------
# Color science: sRGB <-> linear, XYZ/Lab (D65), CIEDE2000
# ---------------------------------------------------------------------------
def srgb_to_linear(c):
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(lin):
    lin = np.clip(np.asarray(lin, dtype=np.float64), 0.0, None)
    return np.where(lin <= 0.0031308, 12.92 * lin,
                    1.055 * np.power(lin, 1.0 / 2.4) - 0.055)


_SRGB_TO_XYZ = np.array([
    [0.41239080, 0.35758434, 0.18048079],
    [0.21263901, 0.71516868, 0.07219232],
    [0.01933082, 0.11919478, 0.95053215],
])
_D65 = np.array([0.95047, 1.0, 1.08883])


def srgb_to_lab(rgb):
    xyz = srgb_to_linear(rgb) @ _SRGB_TO_XYZ.T
    t = xyz / _D65
    f = np.where(t > 0.008856, np.cbrt(t), 7.787 * t + 16.0 / 116.0)
    lab = np.empty_like(f)
    lab[:, 0] = 116.0 * f[:, 1] - 16.0
    lab[:, 1] = 500.0 * (f[:, 0] - f[:, 1])
    lab[:, 2] = 200.0 * (f[:, 1] - f[:, 2])
    return lab


def delta_e_2000(lab1, lab2):
    """CIEDE2000 color difference (Sharma et al. formulation), vectorized."""
    lab1 = np.asarray(lab1, dtype=np.float64)
    lab2 = np.asarray(lab2, dtype=np.float64)
    L1, a1, b1 = lab1[:, 0], lab1[:, 1], lab1[:, 2]
    L2, a2, b2 = lab2[:, 0], lab2[:, 1], lab2[:, 2]
    C1 = np.hypot(a1, b1)
    C2 = np.hypot(a2, b2)
    C_bar = (C1 + C2) / 2.0
    G = 0.5 * (1.0 - np.sqrt(C_bar ** 7 / (C_bar ** 7 + 25.0 ** 7)))
    a1p = a1 * (1.0 + G)
    a2p = a2 * (1.0 + G)
    C1p = np.hypot(a1p, b1)
    C2p = np.hypot(a2p, b2)

    def hue(ap, b):
        h = np.degrees(np.arctan2(b, ap)) % 360.0
        return np.where((ap == 0) & (b == 0), 0.0, h)

    h1p = hue(a1p, b1)
    h2p = hue(a2p, b2)
    dLp = L2 - L1
    dCp = C2p - C1p
    dhp = h2p - h1p
    dhp = np.where(C1p * C2p == 0, 0.0, dhp)
    dhp = np.where(dhp > 180.0, dhp - 360.0, dhp)
    dhp = np.where(dhp < -180.0, dhp + 360.0, dhp)
    dHp = 2.0 * np.sqrt(C1p * C2p) * np.sin(np.radians(dhp) / 2.0)
    L_bar = (L1 + L2) / 2.0
    C_bar_p = (C1p + C2p) / 2.0
    h_sum = h1p + h2p
    h_diff = np.abs(h1p - h2p)
    h_bar = np.where(C1p * C2p == 0, h_sum,
                     np.where(h_diff <= 180.0, h_sum / 2.0,
                              np.where(h_sum < 360.0, (h_sum + 360.0) / 2.0,
                                       (h_sum - 360.0) / 2.0)))
    T = (1.0 - 0.17 * np.cos(np.radians(h_bar - 30.0))
         + 0.24 * np.cos(np.radians(2.0 * h_bar))
         + 0.32 * np.cos(np.radians(3.0 * h_bar + 6.0))
         - 0.20 * np.cos(np.radians(4.0 * h_bar - 63.0)))
    d_theta = 30.0 * np.exp(-(((h_bar - 275.0) / 25.0) ** 2))
    R_C = 2.0 * np.sqrt(C_bar_p ** 7 / (C_bar_p ** 7 + 25.0 ** 7))
    S_L = 1.0 + (0.015 * (L_bar - 50.0) ** 2) / np.sqrt(20.0 + (L_bar - 50.0) ** 2)
    S_C = 1.0 + 0.045 * C_bar_p
    S_H = 1.0 + 0.015 * C_bar_p * T
    R_T = -np.sin(np.radians(2.0 * d_theta)) * R_C
    return np.sqrt((dLp / S_L) ** 2 + (dCp / S_C) ** 2 + (dHp / S_H) ** 2
                   + R_T * (dCp / S_C) * (dHp / S_H))


# ---------------------------------------------------------------------------
# Chart extraction
# ---------------------------------------------------------------------------
def _bilinear_sample(img, xs, ys):
    """Sample img (H,W,3) float at fractional coords with bilinear interp."""
    h, w = img.shape[:2]
    x0 = np.floor(xs).astype(np.int64)
    y0 = np.floor(ys).astype(np.int64)
    x1 = np.minimum(x0 + 1, w - 1)
    y1 = np.minimum(y0 + 1, h - 1)
    x0 = np.clip(x0, 0, w - 1)
    y0 = np.clip(y0, 0, h - 1)
    fx = (xs - np.floor(xs))[..., None]
    fy = (ys - np.floor(ys))[..., None]
    top = img[y0, x0] * (1 - fx) + img[y0, x1] * fx
    bot = img[y1, x0] * (1 - fx) + img[y1, x1] * fx
    return top * (1 - fy) + bot * fy


def sample_patches_from_quad(img, corners):
    """Mean sRGB (24,3) 0..1 from a chart quad.

    corners: (4,2) in TL, TR, BR, BL order; patch (row 0, col 0) at the TL
    corner is Dark Skin. Each of the 6x4 cells contributes its center 40%.
    """
    corners = np.asarray(corners, dtype=np.float64).reshape(4, 2)
    tl, tr, br, bl = corners
    patches = np.zeros((24, 3), dtype=np.float64)
    n = 9  # 9x9 sample points per patch
    for row in range(4):
        for col in range(6):
            u0, u1 = col / 6.0, (col + 1) / 6.0
            v0, v1 = row / 4.0, (row + 1) / 4.0
            us = np.linspace(u0 + 0.30 * (u1 - u0), u0 + 0.70 * (u1 - u0), n)
            vs = np.linspace(v0 + 0.30 * (v1 - v0), v0 + 0.70 * (v1 - v0), n)
            uu, vv = np.meshgrid(us, vs)
            # bilinear interpolation across the quad
            top = tl[None, None, :] * (1 - uu[..., None]) + tr[None, None, :] * uu[..., None]
            bot = bl[None, None, :] * (1 - uu[..., None]) + br[None, None, :] * uu[..., None]
            pts = top * (1 - vv[..., None]) + bot * vv[..., None]
            vals = _bilinear_sample(img, pts[..., 0], pts[..., 1])
            patches[row * 6 + col] = vals.reshape(-1, 3).mean(axis=0)
    return patches


def _fix_patch_orientation(patches):
    """Heuristics for detector-found charts: fix R/B swap and 180° rotation.

    Returns (patches, notes). Safe on already-correct input: the checks only
    trigger on strongly contradictory evidence.
    """
    notes = []
    p = patches.copy()
    # R/B swap: patch 13 "Blue" must be blue-dominant, patch 15 "Red" red-dominant.
    if p[12, 2] < p[12, 0] and p[14, 0] < p[14, 2]:
        p = p[:, ::-1].copy()
        notes.append("swapped R/B channels (detector returned BGR order)")
    # 180° rotation: neutral ramp (patches 19..24) must decrease in brightness.
    ramp = p[18:24] @ np.array([0.2126, 0.7152, 0.0722])
    if np.all(np.diff(ramp) > 0):
        p = p[::-1].copy()
        notes.append("reversed patch order (chart detected rotated 180 degrees)")
    return p, notes


def detect_chart_corners_mcc(img_rgb):
    """Use OpenCV's mcc ColorChecker detector; return (corners (4,2), notes).

    Raises RuntimeError with guidance if detection is unavailable/fails.
    """
    try:
        import cv2
    except ImportError:
        raise RuntimeError("OpenCV (cv2) is not installed")
    detector = None
    mcc = getattr(cv2, "mcc", None)
    if mcc is not None and hasattr(mcc, "CCheckerDetector_create"):
        detector = mcc.CCheckerDetector_create()          # OpenCV >= 4.5 / 5.x
    elif hasattr(cv2, "mcc_ColorCheckerDetector"):
        detector = cv2.mcc_ColorCheckerDetector.create()  # OpenCV 4.x legacy
    if detector is None:
        raise RuntimeError("this OpenCV build has no mcc ColorChecker detector "
                           "(install opencv-contrib-python-headless)")
    img_bgr = np.ascontiguousarray(
        (np.clip(img_rgb, 0.0, 1.0) * 255.0).astype(np.uint8)[:, :, ::-1])
    chart_type = getattr(mcc, "MCC24", None) if mcc is not None else None
    if chart_type is not None:
        try:
            detector.setColorChartType(chart_type)
        except Exception:
            pass
    # OpenCV 5: process(image) — chart type set above (its 2nd positional
    # argument is a chart COUNT, not the type). OpenCV 4: process(image,
    # chartType). Try the modern call first, fall back to the legacy one.
    ok = False
    try:
        ok = bool(detector.process(img_bgr))
    except TypeError:
        try:
            ok = bool(detector.process(img_bgr, chart_type))
        except Exception as exc:
            raise RuntimeError(f"mcc detector failed: {exc}")
    except Exception as exc:
        raise RuntimeError(f"mcc detector failed: {exc}")
    chart = None
    if ok:
        try:
            charts = detector.getListColorChecker()
            if charts:
                chart = charts[0]
        except Exception:
            chart = None
        if chart is None:
            try:
                chart = detector.getBestColorChecker()
            except Exception:
                chart = None
    if chart is None:
        raise RuntimeError("mcc detector did not find a ColorChecker chart")
    box = np.asarray(chart.getBox(), dtype=np.float64).reshape(4, 2)
    # getBox() outlines the whole chart card including its black border;
    # shrink ~1.5% per side so the sampling grid lands on the patch area.
    centroid = box.mean(axis=0)
    box = centroid + (box - centroid) * 0.97
    return box, ["chart quad from OpenCV mcc detector (getBox, inset 1.5%)"]


def load_image_rgb(path):
    """Load an image as float RGB 0..1. Returns (img, loader_name)."""
    try:
        import cv2
        bgr = cv2.imread(path, cv2.IMREAD_COLOR)
        if bgr is None:
            raise RuntimeError(f"could not read image: {path}")
        return bgr[:, :, ::-1].astype(np.float64) / 255.0, "opencv"
    except ImportError:
        pass
    try:
        from PIL import Image
        with Image.open(path) as im:
            return np.asarray(im.convert("RGB"), dtype=np.float64) / 255.0, "pillow"
    except ImportError:
        raise RuntimeError(
            "no image loader available: install opencv-contrib-python-headless "
            "(recommended) or pillow")


# ---------------------------------------------------------------------------
# Polynomial correction fit (linear RGB)
# ---------------------------------------------------------------------------
def poly_features(lin):
    """(N,3) linear RGB -> (N,11): [1, r, g, b, rg, rb, gb, r^2, g^2, b^2, rgb]."""
    lin = np.asarray(lin, dtype=np.float64)
    r, g, b = lin[:, 0], lin[:, 1], lin[:, 2]
    return np.stack([np.ones_like(r), r, g, b, r * g, r * b, g * b,
                     r * r, g * g, b * b, r * g * b], axis=1)


def fit_correction(measured_srgb, reference_srgb=REFERENCE_SRGB):
    """Least-squares polynomial mapping measured -> reference (linear RGB)."""
    f_meas = poly_features(srgb_to_linear(measured_srgb))
    coef, *_ = np.linalg.lstsq(f_meas, srgb_to_linear(reference_srgb),
                               rcond=None)
    return coef


def apply_correction(coef, srgb):
    lin = poly_features(srgb_to_linear(srgb)) @ coef
    return linear_to_srgb(np.clip(lin, 0.0, 1.0))


def build_correction_lut(coef):
    """(33,33,33,3) LUT array [b,g,r,ch] by evaluating the fit on the grid."""
    nodes = grid_nodes()
    corrected = apply_correction(coef, nodes)
    return corrected.reshape(LUT_SIZE, LUT_SIZE, LUT_SIZE, 3)


# ---------------------------------------------------------------------------
# Pack building
# ---------------------------------------------------------------------------
def compose_device_lut(style_lut, corr_lut):
    """out(node) = correction(style(node)) via trilinear sampling."""
    nodes = grid_nodes()
    styled = sample_lut(style_lut, nodes)
    return sample_lut(corr_lut, styled)


def load_generic_config(assets_luts):
    cfg_path = os.path.join(assets_luts, "config.json")
    with open(cfg_path, encoding="utf-8") as f:
        cfg = json.load(f)
    return {entry["id"]: entry for entry in cfg.get("luts", [])}


def update_devices_index(assets_luts, key, manufacturer, models):
    devices_dir = os.path.join(assets_luts, "devices")
    os.makedirs(devices_dir, exist_ok=True)
    index_path = os.path.join(devices_dir, "index.json")
    index = {"packs": []}
    if os.path.exists(index_path):
        with open(index_path, encoding="utf-8") as f:
            index = json.load(f)
    packs = [p for p in index.get("packs", []) if p.get("key") != key]
    packs.append({"key": key, "manufacturer": manufacturer,
                  "models": list(models), "folder": key})
    index["packs"] = packs
    with open(index_path, "w", encoding="utf-8") as f:
        json.dump(index, f, ensure_ascii=False, indent=2)
        f.write("\n")
    return index_path


def run_calibration(chart_path, assets_luts, device_key, manufacturer, models,
                    style_ids, corners=None):
    """Full pipeline. Returns (report_dict, pack_dir)."""
    img, loader = load_image_rgb(chart_path)
    notes = [f"image loaded with {loader}"]
    if corners is not None:
        measured = sample_patches_from_quad(img, corners)
        notes.append("patches sampled from --corners quad")
    else:
        box, det_notes = detect_chart_corners_mcc(img)
        measured = sample_patches_from_quad(img, box)
        measured, fix_notes = _fix_patch_orientation(measured)
        notes.extend(det_notes)
        notes.extend(fix_notes)
    measured = np.clip(measured, 0.0, 1.0)

    coef = fit_correction(measured)
    corr_lut = build_correction_lut(coef)
    corrected = sample_lut(corr_lut, measured)

    ref_lab = srgb_to_lab(REFERENCE_SRGB)
    de_before = delta_e_2000(srgb_to_lab(measured), ref_lab)
    de_after = delta_e_2000(srgb_to_lab(corrected), ref_lab)

    # ---- compose device LUTs and write the pack ---------------------------
    generic = load_generic_config(assets_luts)
    pack_dir = os.path.join(assets_luts, "devices", device_key)
    os.makedirs(pack_dir, exist_ok=True)
    pack_entries = []
    written = []
    for sid in style_ids:
        entry = generic.get(sid)
        if entry is None:
            raise ValueError(f"style id '{sid}' not found in generic config.json "
                             f"(have: {', '.join(sorted(generic))})")
        rel_path = entry.get("path", "")
        if not rel_path:
            print(f"  note: style '{sid}' has no LUT file (path empty); skipped")
            continue
        _, style_lut = read_plut(os.path.join(assets_luts, rel_path))
        device_rgb = compose_device_lut(style_lut, corr_lut)
        file_name = os.path.basename(rel_path)
        write_plut(os.path.join(pack_dir, file_name), device_rgb)
        new_entry = copy.deepcopy(entry)
        new_entry["path"] = file_name
        pack_entries.append(new_entry)
        written.append(file_name)
    with open(os.path.join(pack_dir, "config.json"), "w", encoding="utf-8") as f:
        json.dump({"luts": pack_entries}, f, ensure_ascii=False, indent=2)
        f.write("\n")
    update_devices_index(assets_luts, device_key, manufacturer, models)

    report = {
        "device": {"key": device_key, "manufacturer": manufacturer,
                   "models": list(models)},
        "chart_image": os.path.abspath(chart_path),
        "styles": [e["id"] for e in pack_entries],
        "files": written,
        "notes": notes,
        "delta_e_2000": {
            "mean_before": float(de_before.mean()),
            "max_before": float(de_before.max()),
            "mean_after": float(de_after.mean()),
            "max_after": float(de_after.max()),
        },
        "patches": [
            {
                "index": i + 1,
                "name": PATCH_NAMES[i],
                "measured_srgb": [round(float(v * 255), 1) for v in measured[i]],
                "reference_srgb": [float(v) for v in REFERENCE_SRGB_255[i]],
                "corrected_srgb": [round(float(v * 255), 1) for v in corrected[i]],
                "delta_e_before": float(de_before[i]),
                "delta_e_after": float(de_after[i]),
            }
            for i in range(24)
        ],
    }
    with open(os.path.join(pack_dir, "report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
        f.write("\n")
    return report, pack_dir


# ---------------------------------------------------------------------------
# Self-test: synthetic chart + known distortion -> full pipeline -> asserts
# ---------------------------------------------------------------------------
def _save_image(path, img_rgb_uint8):
    try:
        import cv2
        if cv2.imwrite(path, img_rgb_uint8[:, :, ::-1],
                       [cv2.IMWRITE_JPEG_QUALITY, 97]):
            return
    except ImportError:
        pass
    from PIL import Image
    Image.fromarray(img_rgb_uint8, "RGB").save(path, quality=97)


def self_test():
    print("=== calibrate.py self-test ===")
    styles = [s.strip() for s in DEFAULT_STYLES.split(",") if s.strip()]
    with tempfile.TemporaryDirectory(prefix="leica-calib-") as tmp:
        # -- temp assets dir: copies of the real generic LUTs + config -------
        import shutil
        assets = os.path.join(tmp, "luts")
        shutil.copytree(DEFAULT_ASSETS, assets,
                        ignore=shutil.ignore_patterns("devices"))

        # -- synthesize a chart photo with a KNOWN distortion ----------------
        cell, margin_x, margin_y = 176, 70, 60
        width, height = margin_x * 2 + cell * 6, margin_y * 2 + cell * 4
        img = np.full((height, width, 3), 24, dtype=np.uint8)
        distort_m = np.array([[1.07, 0.04, -0.01],
                              [0.02, 0.96,  0.03],
                              [-0.02, 0.05, 0.91]])
        gamma = 1.08
        ref_lin = srgb_to_linear(REFERENCE_SRGB)
        dist_lin = np.clip((ref_lin @ distort_m.T) ** gamma, 0.0, 1.0)
        dist_srgb = np.clip(linear_to_srgb(dist_lin), 0.0, 1.0)
        for i in range(24):
            row, col = divmod(i, 6)
            y0, x0 = margin_y + row * cell, margin_x + col * cell
            img[y0:y0 + cell, x0:x0 + cell] = np.round(
                dist_srgb[i] * 255).astype(np.uint8)
        chart_path = os.path.join(tmp, "synthetic_chart.jpg")
        _save_image(chart_path, img)
        corners = np.array([[margin_x, margin_y],
                            [margin_x + cell * 6, margin_y],
                            [margin_x + cell * 6, margin_y + cell * 4],
                            [margin_x, margin_y + cell * 4]], dtype=np.float64)
        print(f"synthetic chart: {chart_path} ({width}x{height}), "
              f"distortion = 3x3 matrix + gamma {gamma} in linear RGB")

        report, pack_dir = run_calibration(
            chart_path, assets, device_key="selftest-device",
            manufacturer="SelfTest", models=["TEST0001"],
            style_ids=styles, corners=corners)
        de = report["delta_e_2000"]
        print(f"Delta E 2000 (in-sample): mean {de['mean_before']:.3f} -> "
              f"{de['mean_after']:.3f} | max {de['max_before']:.3f} -> "
              f"{de['max_after']:.3f}")

        failures = []
        for entry in report["files"]:
            p = os.path.join(pack_dir, entry)
            if not os.path.exists(p):
                failures.append(f"missing pack file {entry}")
            elif os.path.getsize(p) != PLUT_BYTES_UINT16_33:
                failures.append(f"{entry}: {os.path.getsize(p)} bytes "
                                f"(expected {PLUT_BYTES_UINT16_33})")
        if len(report["files"]) != len(styles):
            failures.append(f"expected {len(styles)} pack files, "
                            f"got {len(report['files'])}")
        for extra in ("config.json", "report.json"):
            if not os.path.exists(os.path.join(pack_dir, extra)):
                failures.append(f"missing {extra} in pack")
        if not os.path.exists(os.path.join(assets, "devices", "index.json")):
            failures.append("devices/index.json not written")
        if not de["mean_after"] < de["mean_before"]:
            failures.append("mean after-ΔE is not better than before")
        if not de["mean_after"] < 3.0:
            failures.append(f"mean after-ΔE {de['mean_after']:.3f} >= 3.0")
        if failures:
            print("SELF-TEST FAIL:")
            for f_ in failures:
                print(f"  - {f_}")
            return 1
        print(f"pack files OK: {len(report['files'])} x .plut "
              f"({PLUT_BYTES_UINT16_33} bytes each) + config.json + report.json")
        print("SELF-TEST PASS")
        return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def parse_corners(text):
    try:
        vals = [float(v) for v in text.split(",")]
    except ValueError:
        raise argparse.ArgumentTypeError(
            "--corners must be 8 comma-separated numbers: x1,y1,...,x4,y4")
    if len(vals) != 8:
        raise argparse.ArgumentTypeError(
            f"--corners needs exactly 8 numbers, got {len(vals)}")
    return np.array(vals, dtype=np.float64).reshape(4, 2)


INSTALL_HELP = """\
ERROR: OpenCV (cv2) is not installed and --corners was not given, so the
ColorChecker chart cannot be auto-detected.

Install the detector dependency with:

    pip install opencv-contrib-python-headless

(the "contrib" build is required — plain opencv-python has no mcc module),
or re-run with --corners x1,y1,x2,y2,x3,y3,x4,y4 giving the chart's four
outer corners (top-left, top-right, bottom-right, bottom-left; the
top-left patch must be Dark Skin). The --corners path only needs an image
loader: opencv (above) or pillow (pip install pillow)."""


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Build a per-device LUT pack for leicafujicam from a "
                    "ColorChecker 24 photo.")
    ap.add_argument("--chart", help="photo of the ColorChecker 24 chart "
                                    "(shot through the app, LUT = None)")
    ap.add_argument("--device-key", help="slug for the pack folder, "
                                         "e.g. oppo-find-x8-pro")
    ap.add_argument("--manufacturer", help="device manufacturer, e.g. OPPO")
    ap.add_argument("--models", help="comma-separated Build.MODEL codes, "
                                     "e.g. PKC110,PKC130")
    ap.add_argument("--styles", default=DEFAULT_STYLES,
                    help="comma-separated LUT ids (default: %(default)s)")
    ap.add_argument("--assets-luts", default=DEFAULT_ASSETS,
                    help="path to app/src/main/assets/luts "
                         "(default: %(default)s)")
    ap.add_argument("--corners", type=parse_corners, default=None,
                    metavar="x1,y1,...,x4,y4",
                    help="manual chart corners (TL,TR,BR,BL) — skips "
                         "auto-detection")
    ap.add_argument("--self-test", action="store_true",
                    help="synthesize a distorted chart and verify the whole "
                         "pipeline (no real chart needed)")
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test()

    missing = [name for name, val in
               (("--chart", args.chart), ("--device-key", args.device_key),
                ("--manufacturer", args.manufacturer), ("--models", args.models))
               if not val]
    if missing:
        ap.error(f"the following arguments are required: {', '.join(missing)}")
    if not os.path.isdir(args.assets_luts):
        print(f"ERROR: assets luts dir not found: {args.assets_luts}",
              file=sys.stderr)
        return 2
    models = [m.strip() for m in args.models.split(",") if m.strip()]
    style_ids = [s.strip() for s in args.styles.split(",") if s.strip()]

    if args.corners is None:
        try:
            import cv2  # noqa: F401
        except ImportError:
            print(INSTALL_HELP, file=sys.stderr)
            return 2

    try:
        report, pack_dir = run_calibration(
            args.chart, args.assets_luts, args.device_key, args.manufacturer,
            models, style_ids, corners=args.corners)
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        if args.corners is None:
            print("Tip: if auto-detection keeps failing, re-run with "
                  "--corners (see README.md).", file=sys.stderr)
        return 1
    except (ValueError, FileNotFoundError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    de = report["delta_e_2000"]
    print(f"pack written: {pack_dir}")
    print(f"styles: {', '.join(report['styles'])}")
    print(f"Delta E 2000 (in-sample): mean {de['mean_before']:.3f} -> "
          f"{de['mean_after']:.3f} | max {de['max_before']:.3f} -> "
          f"{de['max_after']:.3f}")
    print(f"report: {os.path.join(pack_dir, 'report.json')}")
    if de["mean_after"] >= de["mean_before"]:
        print("\n**********************************************************")
        print("WARNING: corrected mean Delta E is NOT better than the")
        print("uncorrected value. The pack was written anyway, but do NOT")
        print("ship it — check the chart photo (lighting, glare, patch")
        print("order) and the full report.json before trusting it.")
        print("**********************************************************")
    return 0


if __name__ == "__main__":
    sys.exit(main())
