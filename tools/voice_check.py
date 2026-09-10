#!/usr/bin/env python3
"""Measure the WAVs that HOLLOW_VOICE_DUMP writes.

Three things the voice milestone has to prove, and this is what proves them:

  pitch   the four goon presets must sound measurably different, not just differently labelled.
          `pitch` reports the fundamental of each file (harmonic-product-spectrum over the FFT,
          which is robust on speech where the loudest bin is often a harmonic, not F0) and the
          spectral centroid, which is what the formant setting moves.

  level   proximity has to actually attenuate. `level` prints an RMS envelope in fixed windows,
          in dBFS, so it can be lined up against the `voice:` log lines, which carry the distance
          and applied gain for the same second.

  gaps    packet loss concealment has to keep the audio continuous. `gaps` reports the longest run
          of silence after the stream starts, in milliseconds. Anything over ~60 ms with the
          jitter buffer running is a dropout you would hear.

Usage:
    tools/voice_check.py pitch  FILE [FILE ...]
    tools/voice_check.py level  FILE [--window 0.5]
    tools/voice_check.py gaps   FILE [--floor -60]
"""
import argparse
import math
import sys
import wave

import numpy as np


def read_wav(path):
    """-> (mono float32 in [-1,1], sample rate). Any channel count, 16-bit PCM."""
    with wave.open(path, "rb") as w:
        n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if sw != 2:
        sys.exit(f"{path}: expected 16-bit PCM, got {sw * 8}-bit")
    x = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, sr


def fundamental(x, sr, fmin=60.0, fmax=500.0):
    """Harmonic product spectrum over the loudest half-second. Returns (f0_hz, centroid_hz)."""
    # Pick the loudest 0.5 s so silence at the head of the file cannot decide the answer.
    win = int(0.5 * sr)
    if len(x) <= win:
        seg = x
    else:
        hop = win // 2
        starts = range(0, len(x) - win, hop)
        best = max(starts, key=lambda s: float(np.sqrt(np.mean(x[s:s + win] ** 2))))
        seg = x[best:best + win]
    if float(np.sqrt(np.mean(seg ** 2))) < 1e-6:
        return 0.0, 0.0
    n = 1 << int(math.ceil(math.log2(len(seg))))
    mag = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
    freq = np.fft.rfftfreq(n, 1.0 / sr)

    centroid = float((freq * mag).sum() / max(mag.sum(), 1e-12))

    # Harmonic product spectrum: multiply the spectrum by decimated copies of itself, so the true
    # fundamental (present in every copy) survives and lone strong harmonics do not.
    hps = mag[: len(mag) // 5].copy()
    for k in (2, 3, 4, 5):
        dec = mag[::k][: len(hps)]
        hps[: len(dec)] *= dec
    lo = int(fmin * n / sr)
    hi = min(int(fmax * n / sr), len(hps) - 1)
    if hi <= lo:
        return 0.0, centroid
    peak = lo + int(np.argmax(hps[lo:hi]))
    return float(freq[peak]), centroid


def cmd_pitch(args):
    print(f"{'file':<44} {'F0 Hz':>8} {'centroid Hz':>12} {'peak':>7} {'rms':>8}")
    for p in args.files:
        x, sr = read_wav(p)
        f0, cen = fundamental(x, sr)
        rms = float(np.sqrt(np.mean(x ** 2))) if len(x) else 0.0
        print(f"{p.split('/')[-1]:<44} {f0:8.1f} {cen:12.1f} "
              f"{float(np.abs(x).max()) if len(x) else 0:7.3f} {rms:8.5f}")


def cmd_level(args):
    x, sr = read_wav(args.file)
    w = max(1, int(args.window * sr))
    print(f"# {args.file}: {len(x) / sr:.2f} s, window {args.window:.2f} s")
    print(f"{'t s':>8} {'rms':>10} {'dBFS':>8}")
    for i in range(0, len(x) - w + 1, w):
        seg = x[i:i + w]
        r = float(np.sqrt(np.mean(seg ** 2)))
        db = 20 * math.log10(r) if r > 1e-9 else -120.0
        print(f"{i / sr:8.2f} {r:10.6f} {db:8.1f}")


def cmd_gaps(args):
    x, sr = read_wav(args.file)
    w = int(0.010 * sr)                      # 10 ms decision windows
    floor = 10 ** (args.floor / 20.0)
    loud = np.array([float(np.sqrt(np.mean(x[i:i + w] ** 2))) > floor
                     for i in range(0, len(x) - w + 1, w)])
    if not loud.any():
        print(f"{args.file}: never rose above {args.floor} dBFS")
        return
    first, last = int(np.argmax(loud)), len(loud) - int(np.argmax(loud[::-1])) - 1
    run = worst = 0
    for v in loud[first:last + 1]:
        run = 0 if v else run + 1
        worst = max(worst, run)
    active = (last - first + 1) * 10
    print(f"{args.file}: audio from {first * 10 / 1000:.2f} s to {last * 10 / 1000:.2f} s "
          f"({active} ms), longest silent run {worst * 10} ms, "
          f"silent {100.0 * (~loud[first:last + 1]).sum() / (last - first + 1):.1f}% of it")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pitch"); p.add_argument("files", nargs="+"); p.set_defaults(fn=cmd_pitch)
    p = sub.add_parser("level"); p.add_argument("file"); p.add_argument("--window", type=float, default=0.5); p.set_defaults(fn=cmd_level)
    p = sub.add_parser("gaps"); p.add_argument("file"); p.add_argument("--floor", type=float, default=-60.0); p.set_defaults(fn=cmd_gaps)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
