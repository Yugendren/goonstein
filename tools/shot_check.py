#!/usr/bin/env python3
"""CI gate: does a screenshot PNG hold an actual rendered scene, or a black/blank frame.

CI screenshots the game headless and the only failure mode worth catching automatically is
"the window came up but nothing drew" -- a black frame, a solid clear-colour frame, a crash
before the first present. That shows up as low mean luma, low variance and almost no distinct
colours, so this checks those three things and nothing else; not an image-diff, just a
"did anything render" smoke test.

Decodes the PNG itself (signature, IHDR/PLTE/IDAT/IEND chunks, zlib inflate, scanline
unfilter) instead of pulling in Pillow or numpy, so it needs nothing beyond the standard
library on the macOS/Linux/Windows CI runners. Only flat, non-interlaced 8-bit images are
supported -- that is everything a screenshot dump writes; anything else raises rather than
silently decoding garbage.

    python3 tools/shot_check.py shot.png
    python3 tools/shot_check.py shot.png --min-mean 0.02 --min-unique 32 --min-nonblack 0.05
    python3 tools/shot_check.py shot.png --json
"""
import argparse, json, math, struct, sys, zlib

SIG = b"\x89PNG\r\n\x1a\n"
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}  # colour type -> samples per pixel

def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)

def unfilter(raw, width, height, channels):
    """Undo the five PNG scanline filters (0 none .. 4 Paeth), 8-bit depth only."""
    stride = width * channels
    out = bytearray(height * stride)
    prev = bytearray(stride)
    pos = 0
    for y in range(height):
        ftype = raw[pos]; pos += 1
        line = bytearray(raw[pos:pos + stride]); pos += stride
        if ftype == 0:
            pass
        elif ftype == 1:  # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ftype == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:  # Average
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:  # Paeth
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                c = prev[i - channels] if i >= channels else 0
                line[i] = (line[i] + paeth(a, prev[i], c)) & 0xFF
        else:
            raise ValueError(f"unknown scanline filter type {ftype} at row {y}")
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return out

def decode_png(path):
    data = open(path, "rb").read()
    if data[:8] != SIG:
        raise ValueError(f"{path}: not a PNG (bad signature)")
    pos, idat, palette, ihdr = 8, [], None, None
    while pos < len(data):
        length, = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        cdata = data[pos + 8:pos + 8 + length]
        pos += 12 + length  # length + type + data + crc
        if ctype == b"IHDR":
            w, h, depth, colour, comp, filt, interlace = struct.unpack(">IIBBBBB", cdata)
            ihdr = (w, h, depth, colour, comp, filt, interlace)
        elif ctype == b"PLTE":
            palette = [tuple(cdata[i:i + 3]) for i in range(0, len(cdata), 3)]
        elif ctype == b"IDAT":
            idat.append(cdata)
        elif ctype == b"IEND":
            break
    if ihdr is None:
        raise ValueError(f"{path}: no IHDR chunk")
    width, height, depth, colour, comp, filt, interlace = ihdr
    if depth != 8:
        raise ValueError(f"{path}: bit depth {depth} not supported, only 8-bit PNGs are")
    if interlace != 0:
        raise ValueError(f"{path}: Adam7-interlaced PNG not supported, re-export non-interlaced")
    if colour not in CHANNELS:
        raise ValueError(f"{path}: colour type {colour} not supported")
    if colour == 3 and not palette:
        raise ValueError(f"{path}: palette colour type but no PLTE chunk")
    channels = CHANNELS[colour]
    raw = zlib.decompress(b"".join(idat))
    px = unfilter(raw, width, height, channels)

    # Sum luma/luma^2 and count quantised colours in one pass -- 1280x800 is ~1M pixels and
    # a second pass in pure Python is the difference between "fast enough for CI" and not.
    n = width * height
    sum_l = sumsq_l = nonblack = 0.0
    seen = set()
    stride = width * channels
    for y in range(height):
        row = y * stride
        for x in range(width):
            o = row + x * channels
            if colour == 0 or colour == 4:
                r = g = b = px[o]
            elif colour == 3:
                r, g, b = palette[px[o]]
            else:  # 2 (RGB) or 6 (RGBA)
                r, g, b = px[o], px[o + 1], px[o + 2]
            luma = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0
            sum_l += luma
            sumsq_l += luma * luma
            if luma > 0.02:
                nonblack += 1
            seen.add((r >> 3, g >> 3, b >> 3))
    mean = sum_l / n
    variance = max(0.0, sumsq_l / n - mean * mean)
    return {
        "width": width, "height": height,
        "mean_luma": mean, "stddev": math.sqrt(variance),
        "unique_colors": len(seen), "nonblack": nonblack / n,
    }

def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("png")
    ap.add_argument("--min-mean", type=float, default=0.02)
    ap.add_argument("--max-mean", type=float, default=0.995)
    ap.add_argument("--min-unique", type=int, default=32)
    ap.add_argument("--min-nonblack", type=float, default=0.05)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()

    try:
        s = decode_png(a.png)
    except (ValueError, zlib.error) as e:
        sys.exit(f"shot_check: {e}")

    print("%s %dx%d mean_luma=%.3f stddev=%.3f unique_colors=%d nonblack=%.2f"
          % (a.png, s["width"], s["height"], s["mean_luma"], s["stddev"],
             s["unique_colors"], s["nonblack"]))
    if a.json:
        print(json.dumps({k: s[k] for k in
                           ("width", "height", "mean_luma", "stddev", "unique_colors", "nonblack")}))

    fails = []
    if s["mean_luma"] < a.min_mean:
        fails.append(f"mean_luma {s['mean_luma']:.3f} < {a.min_mean} -- the frame is essentially black")
    if s["mean_luma"] > a.max_mean:
        fails.append(f"mean_luma {s['mean_luma']:.3f} > {a.max_mean} -- the frame is essentially white")
    if s["unique_colors"] < a.min_unique:
        fails.append(f"unique_colors {s['unique_colors']} < {a.min_unique} -- too flat to be a rendered scene")
    if s["nonblack"] < a.min_nonblack:
        fails.append(f"nonblack {s['nonblack']:.3f} < {a.min_nonblack} -- almost every pixel is near-black")
    if fails:
        for f in fails:
            print(f"FAIL: {f}")
        sys.exit(1)


if __name__ == "__main__":
    main()
