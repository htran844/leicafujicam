# Per-device LUT calibration (offline device packs)

`calibrate.py` turns **one photo of an X-Rite ColorChecker 24 chart**, shot
by a real phone through leicafujicam, into a **device pack**: versions of the
app's style LUTs that have been corrected for that device's actual color
rendering. Packs are bundled with the app (offline direction) and selected
at runtime by device model.

```
chart photo ──► extract 24 patch means ──► polynomial fit (linear RGB,
                                              measured -> reference)
                                           bake 33^3 correction LUT
device LUT(node) = correction( generic_style_LUT(node) )   per style
```

Outputs go to `app/src/main/assets/luts/devices/<device-key>/`:

| file | content |
|---|---|
| `<style>.plut` … | composed device LUTs (PLUT v1, 33³, uint16 — same format as the bundled LUTs) |
| `config.json` | entries copied from the generic `config.json` for those styles (same ids/names/flags) |
| `report.json` | per-patch measured vs reference values and ΔE2000 before/after |

and `devices/index.json` is updated with the pack's key, manufacturer and
model list (`Build.MODEL` codes the pack applies to).

## Install dependencies

Python 3 with numpy, plus OpenCV **contrib** (for chart auto-detection):

```bash
pip install numpy opencv-contrib-python-headless
```

- The `contrib` build is required: plain `opencv-python` does not ship the
  `mcc` ColorChecker detector.
- `pillow` is an optional fallback image loader, only used on the
  `--corners` path when OpenCV is absent.
- scipy is **not** needed.

## Shooting the chart photo (this decides the quality)

Shoot with the phone you are calibrating, **through the app**, with:

- LUT/filter set to **None** (the correction is measured against the app's
  unfiltered rendering).
- A real X-Rite ColorChecker 24 (or faithful reproduction) chart.
- **Even, diffuse D65 / ~5500 K light** — daylight-balanced LED panels or
  open shade on a bright day. No mixed lighting, no colored walls nearby.
- **Lock white balance and exposure** (manual mode): WB fixed (e.g. 5500 K),
  exposure set so the chart is bright but the White patch is **not clipped**.
- Chart **fills most of the frame**, camera parallel to the chart (minimal
  perspective), all 24 patches visible with a small margin.
- **No glare**: angle the light (or yourself) so no patch has a specular
  hotspot; matte chart surface must look uniform. Glare on even one patch
  ruins the fit for its whole color region.
- Shoot 2–3 frames; calibrate from the cleanest one.

Patch order expectation: row-major starting at **Dark Skin** (top-left when
the chart is upright: brown skin patch first, Black patch last, bottom-right).

## Run

Auto-detection (OpenCV mcc):

```bash
cd tools/calibration
python3 calibrate.py \
  --chart /path/to/chart_photo.jpg \
  --device-key oppo-find-x8-pro \
  --manufacturer OPPO \
  --models PKC110,PKC130
```

Manual corners fallback (if detection fails — e.g. cropped or angled shots).
Corners are the chart's outer corners in **TL, TR, BR, BL** order, in image
pixels, with the top-left corner at the Dark Skin patch:

```bash
python3 calibrate.py --chart chart.jpg \
  --device-key pixel-9 --manufacturer Google --models G2YBB,GUR25 \
  --corners 412,388,3512,402,3540,2610,398,2598
```

Options:

- `--styles leica,cc,...` — which generic LUT ids to compose
  (default: `leica,cc,Hasselblad,tomosaki,kodak_portra_400,kodak_gold_200,nc,teal_orange`).
- `--assets-luts <path>` — defaults to `../../app/src/main/assets/luts`
  relative to this script.

Verify the tool itself at any time (no chart needed — it synthesizes a
chart with a known distortion and checks the whole pipeline end-to-end):

```bash
python3 calibrate.py --self-test
```

## Reading the report

`report.json` (also summarized on stdout) contains, per patch: measured
sRGB, reference sRGB, corrected sRGB, and ΔE2000 before/after, plus the
mean/max over all 24 patches. ΔE is **in-sample** (same patches used for
the fit), so treat it as a sanity floor, not proof.

Quality targets (checked on a **separate verification shot** of the chart,
ideally under slightly different light):

- **mean ΔE00 < 3** — good pack.
- **max ΔE00 < 6** — no single patch badly off.
- Worse than that: re-shoot before re-fitting — the usual causes are
  glare, mixed lighting, a clipped White patch, or wrong patch orientation
  (e.g. chart upside down; the neutral ramp in the report should decrease
  monotonically from White 9.5 to Black 2).

If the corrected mean is not better than the uncorrected mean, the tool
prints a loud warning and the pack must not ship.

## How the correction works (and its limits)

- Fit in **linear RGB** with polynomial features
  `[1, r, g, b, rg, rb, gb, r², g², b², rgb]` (least squares, 24 patches).
  This captures white-balance error, channel cross-talk and mild
  tone/gamma differences between devices.
- The fit is baked into a 33³ LUT and composed **after** each style LUT,
  so one measured correction upgrades every style for that device.
- It corrects *color rendering*, not exposure metering, noise, sharpening
  or lens differences, and it is only as good as the chart photo: a
  calibration is valid for the app's pipeline as shot (JPEG path, LUT=None).
