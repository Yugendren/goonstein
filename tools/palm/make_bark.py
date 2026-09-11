#!/usr/bin/env python3
"""Generate `assets/textures/palm_bark_lit.png` from the photo map `assets/textures/palm_bark.jpg`.

The engine multiplies texture x vertex colour x tint in sRGB, and `palm_bark.jpg` (mean RGB about
88,78,69) is far too dark to drop straight onto a model -- the old hand-built palm compensated with
a per-channel tint of (1.68, 1.48, 1.16). This bakes that same gain into the texture itself, resized
to 512x512, with a soft shoulder instead of a hard multiply-and-clip so highlights compress rather
than flattening to solid white.

The brief's suggested shoulder, out = 255*(g*x)/(1 + (g*x)*(g-1)), maps 0 -> 0 but only maps 1 -> 1
when g == 1 (for g=1.68: g/(1+g*(g-1)) = 1.68/2.1424 = 0.784, not 1). Since none of our gains are 1,
this script instead uses the Reinhard-style compression

    f(x) = g*x / (1 + (g-1)*x)

which satisfies both required endpoints exactly (f(0)=0, f(1)=g/g=1), has f'(0) = g (matches the
straight per-channel multiply for dark tones), and is monotone increasing on [0,1] for any g > 0
(f'(x) = g / (1+(g-1)x)^2, whose denominator stays positive there) -- so shadows still get the full
tint while highlights compress smoothly into 1.0 instead of clipping.

Usage:
    python3 tools/palm/make_bark.py
    python3 tools/palm/make_bark.py --gain 1.68 1.48 1.16 --size 512 --preview out.png
"""
import argparse
import os

import numpy as np
from PIL import Image

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_SRC = os.path.join(REPO_ROOT, "assets", "textures", "palm_bark.jpg")
DEFAULT_OUT = os.path.join(REPO_ROOT, "assets", "textures", "palm_bark_lit.png")


def soft_shoulder(x, g):
    """f(x) = g*x / (1 + (g-1)*x): f(0)=0, f(1)=1, f'(0)=g, monotone increasing on [0,1]."""
    return (g * x) / (1.0 + (g - 1.0) * x)


def make_bark(src_path, size, gain):
    src = Image.open(src_path).convert("RGB")
    resized = src.resize((size, size), Image.LANCZOS)
    arr = np.asarray(resized).astype(np.float64)
    x = arr / 255.0

    g = np.asarray(gain, dtype=np.float64)
    out = soft_shoulder(x, g[None, None, :])
    out = np.clip(np.round(out * 255.0), 0, 255).astype(np.uint8)

    mean_before = arr.mean(axis=(0, 1))
    mean_after = out.astype(np.float64).mean(axis=(0, 1))
    return out, mean_before, mean_after


def make_preview(src_path, out_arr, size, path):
    src = Image.open(src_path).convert("RGB").resize((size, size), Image.LANCZOS)
    before = np.asarray(src)
    gap = np.full((size, 8, 3), 255, dtype=np.uint8)
    combo = np.concatenate([before, gap, out_arr], axis=1)
    Image.fromarray(combo, mode="RGB").save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--gain", type=float, nargs=3, default=[1.68, 1.48, 1.16], metavar=("R", "G", "B"))
    ap.add_argument("--size", type=int, default=512)
    ap.add_argument("--preview", default=None)
    args = ap.parse_args()

    out_arr, mean_before, mean_after = make_bark(args.src, args.size, args.gain)

    print(f"gain: R={args.gain[0]:.3f} G={args.gain[1]:.3f} B={args.gain[2]:.3f}")
    print(f"mean RGB before: {mean_before[0]:.1f},{mean_before[1]:.1f},{mean_before[2]:.1f}")
    print(f"mean RGB after:  {mean_after[0]:.1f},{mean_after[1]:.1f},{mean_after[2]:.1f}")

    # Sanity-check the shoulder curve itself: 0->0, 1->1, monotone, slope g at the origin.
    xs = np.linspace(0.0, 1.0, 2049)
    for ch, g in zip("RGB", args.gain):
        ys = soft_shoulder(xs, g)
        monotone = bool(np.all(np.diff(ys) >= -1e-12))
        slope0 = (soft_shoulder(1e-6, g) - soft_shoulder(0.0, g)) / 1e-6
        print(
            f"  shoulder[{ch}] g={g:.3f}: f(0)={ys[0]:.4f} f(1)={ys[-1]:.4f} "
            f"monotone={monotone} slope@0~{slope0:.3f}"
        )

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    Image.fromarray(out_arr, mode="RGB").save(args.out)
    print(f"wrote {args.out} ({args.size}x{args.size})")

    if args.preview:
        os.makedirs(os.path.dirname(args.preview), exist_ok=True)
        make_preview(args.src, out_arr, args.size, args.preview)
        print(f"wrote preview {args.preview}")


if __name__ == "__main__":
    main()
