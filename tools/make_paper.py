#!/usr/bin/env python3
"""Generate the sketchbook paper grain used by `look sketch` (see shaders/post.frag, the
`paper` sampler, and the `ink.w` "paper amount" / `film.w` "paper tile size in pixels"
uniforms). Writes assets/textures/paper.png, which the shader multiplies over the whole
graded frame, tiling it by dividing screen pixels by `film.w`.

Because a sampler2D with GL_REPEAT gets tiled edge-to-edge across the whole screen, the
texture has to repeat with no visible seam -- one mismatched pixel row would draw a straight
line across every tile boundary on screen. And because it is *multiplied* into the final,
tone-mapped frame rather than blended, any single layer with real punch turns into visible
banding once the whole image is graded; every octave below is kept close to flat on purpose.

Usage:
    python3 tools/make_paper.py                      # writes assets/textures/paper.png, 512x512
    python3 tools/make_paper.py --size 512 --seed 7 --out assets/textures/paper.png
    python3 tools/make_paper.py --check               # also print the seam-vs-interior numbers
"""
import argparse
import os

import numpy as np
from PIL import Image

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
DEFAULT_OUT = os.path.join(REPO_ROOT, "assets", "textures", "paper.png")


def seamless_noise(rng, size, low_cycles, high_cycles, anisotropy=(1.0, 1.0)):
    """Zero-mean, unit-std noise, band-limited between low_cycles and high_cycles complete
    waves per tile, that repeats exactly across the tile.

    A tiled sampler needs edges that match exactly, not approximately -- a value-noise grid
    blended toward its own opposite edge still leaves a seam where the blend falls off. The
    fix is to only ever use frequencies that already complete a whole number of cycles across
    the tile: start from white noise, keep a ring of frequencies in the DFT (each of which is,
    by definition of the DFT, periodic on the tile), and inverse-transform. Every wave in the
    sum lines up with itself at the wrap-around, so the sum does too, exactly, in floating
    point, with no seam-blend needed.

    anisotropy narrows the ring along one axis and widens it along the other, which stretches
    the real-space result into streaks running along the axis that was narrowed in frequency
    (a feature that only varies slowly along x has energy concentrated near kx=0) -- this is
    how the fine "tooth" layer gets turned into fibres instead of isotropic static.
    """
    white = rng.normal(size=(size, size))
    spec = np.fft.fft2(white)
    freqs = np.fft.fftfreq(size) * size  # cycles-per-tile at each DFT bin, including negatives
    fx, fy = np.meshgrid(freqs / anisotropy[0], freqs / anisotropy[1])
    radius = np.sqrt(fx * fx + fy * fy)
    centre = (low_cycles + high_cycles) * 0.5
    width = max((high_cycles - low_cycles) * 0.5, 0.5)
    band = np.exp(-((radius - centre) ** 2) / (2 * width * width))
    band[0, 0] = 0.0  # drop DC: the base colour supplies the mean, the noise should not
    field = np.fft.ifft2(spec * band).real
    return field / (field.std() + 1e-9)


def speckle(rng, size, count, amp_range, sigma_range):
    """A sparse scatter of small dark flecks -- single stray fibres pressed into the sheet.

    These are placed and drawn independently of the two noise octaves above (a handful of
    point features do not need a frequency-domain treatment), but each one is still splatted
    with wrapped coordinates (mod size) rather than clipped at the image edge, so a speck that
    lands near a border still tiles: half of it reappears on the opposite side, exactly as it
    would if the sheet actually continued past that edge.
    """
    field = np.zeros((size, size), dtype=np.float64)
    for _ in range(count):
        cx, cy = rng.integers(0, size), rng.integers(0, size)
        sigma = rng.uniform(*sigma_range)
        amp = rng.uniform(*amp_range)
        r = max(int(np.ceil(3 * sigma)), 1)
        dx = np.arange(-r, r + 1)
        gx, gy = np.meshgrid(dx, dx)
        blob = amp * np.exp(-(gx * gx + gy * gy) / (2 * sigma * sigma))
        xs = (cx + gx) % size
        ys = (cy + gy) % size
        field[ys, xs] -= blob
    return field


def make_paper(size, seed):
    rng = np.random.default_rng(seed)

    # Base: near-white, warm (red > green > blue), the colour of the sheet with no grain at all.
    base = np.array([238.0, 234.0, 224.0])

    # Tooth: fine, high-frequency noise standing in for the felted fibres pressed into real
    # paper under a raking light. Kept low amplitude (+/- ~6 levels) and slightly anisotropic
    # so it reads as short fibres rather than a uniform sanded texture or a video static plate.
    tooth = seamless_noise(rng, size, low_cycles=90, high_cycles=170, anisotropy=(0.45, 1.0))
    tooth = np.clip(tooth, -2.0, 2.0) * (6.0 / 2.0)

    # Mottle: a much slower, larger-scale variation (a handful of cycles across the whole
    # tile) so the sheet reads as having thick and thin patches -- the way real paper's fibre
    # density drifts over a few centimetres -- rather than as flat colour plus uniform noise,
    # which is what tooth alone would look like from a normal viewing distance.
    mottle = seamless_noise(rng, size, low_cycles=2, high_cycles=5, anisotropy=(1.0, 1.0))
    mottle = np.clip(mottle, -2.0, 2.0) * (5.0 / 2.0)

    # Specks: a few hundred single darker fibres, sparse enough to read as flecks rather than
    # as a third noise octave.
    specks = speckle(rng, size, count=280, amp_range=(6.0, 20.0), sigma_range=(0.5, 1.2))

    # Tooth is applied evenly across channels -- it is a brightness texture, not a colour one.
    # Mottle instead gets uneven per-channel weight, so a patch that mottle makes brighter also
    # gets very slightly warmer, and a patch it makes darker very slightly cooler -- a hue
    # drift tied to the same slow field, rather than flat grey brightness change, which is what
    # keeps it from reading as a single "-5..+5 on every channel" darkening pass.
    tooth_gain = np.array([1.0, 1.0, 1.0])
    mottle_gain = np.array([1.00, 0.88, 0.68])

    img = (
        base[None, None, :]
        + tooth[:, :, None] * tooth_gain[None, None, :]
        + mottle[:, :, None] * mottle_gain[None, None, :]
        + specks[:, :, None]
    )
    return np.clip(np.round(img), 0, 255).astype(np.uint8)


def check_seamless(arr):
    a = arr.astype(np.float64)
    col_diffs = np.mean(np.abs(np.diff(a, axis=1)), axis=(0, 2))  # column j vs j+1, all j
    row_diffs = np.mean(np.abs(np.diff(a, axis=0)), axis=(1, 2))  # row i vs i+1, all i
    seam_col = np.mean(np.abs(a[:, -1, :] - a[:, 0, :]))  # last column vs first (the wrap)
    seam_row = np.mean(np.abs(a[-1, :, :] - a[0, :, :]))  # last row vs first (the wrap)
    print("seamlessness check (mean abs diff per channel, adjacent pixel pairs):")
    print(f"  adjacent columns: min={col_diffs.min():.4f} mean={col_diffs.mean():.4f} max={col_diffs.max():.4f}")
    print(f"  wrap seam (col N-1 vs col 0): {seam_col:.4f}")
    print(f"  adjacent rows:    min={row_diffs.min():.4f} mean={row_diffs.mean():.4f} max={row_diffs.max():.4f}")
    print(f"  wrap seam (row N-1 vs row 0): {seam_row:.4f}")
    ok = seam_col <= col_diffs.max() * 1.5 and seam_row <= row_diffs.max() * 1.5
    print(f"  wrap seam within range of ordinary adjacent pairs: {ok}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--size", type=int, default=512)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true", help="print the seam-vs-interior numbers")
    args = ap.parse_args()

    arr = make_paper(args.size, args.seed)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    Image.fromarray(arr, mode="RGB").save(args.out)
    print(f"wrote {args.out} ({arr.shape[1]}x{arr.shape[0]})")
    for i, name in enumerate("RGB"):
        ch = arr[:, :, i].astype(np.float64)
        print(f"  {name}: min={ch.min():.0f} max={ch.max():.0f} mean={ch.mean():.2f}")

    if args.check:
        check_seamless(arr)


if __name__ == "__main__":
    main()
