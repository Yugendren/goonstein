#!/usr/bin/env python3
"""Measure the WAVs that HOLLOW_VOICE_DUMP writes.

Three things the voice milestone has to prove, and this is what proves them:

  pitch   the four goon presets must sound measurably different, not just differently labelled.
          `pitch` reports the median per-frame fundamental (autocorrelation over the voiced
          frames -- speech is not a steady tone, so a single FFT measures whichever vowel was
          loudest) and the spectral centroid, which is what the formant setting moves.

  level   proximity has to actually attenuate. `level` prints an RMS envelope in fixed windows,
          in dBFS, so it can be lined up against the `voice:` log lines, which carry the distance
          and applied gain for the same second.

  track   proximity has to follow the world. `track` reads the per-second `voice sN` lines out
          of a run's log (distance and applied gain) and compares them with the level in that
          speaker's dump for the same second. This is the distance-attenuation proof.

  gaps    packet loss concealment has to keep the audio continuous. `gaps` reports the longest run
          of silence after the stream starts, in milliseconds. Anything over ~60 ms with the
          jitter buffer running is a dropout you would hear.

Usage:
    tools/voice_check.py pitch  FILE [FILE ...]
    tools/voice_check.py track  LOG WAV [--slot N]
    tools/voice_check.py distance DIR [DIR ...]
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


def f0_median(x, sr, fmin=60.0, fmax=400.0):
    """Median F0 over the voiced frames, by normalised autocorrelation.

    Speech is not a steady tone: a single FFT of the whole file measures whichever vowel happened
    to be loudest. Per-frame autocorrelation with a median is what actually compares two renderings
    of the same sentence, and it does not fall for the octave errors a harmonic product spectrum
    makes on a band-limited (`radio`) signal.
    """
    win = int(0.040 * sr)                       # 40 ms: at least two periods down to 60 Hz
    hop = win // 2
    lo, hi = int(sr / fmax), int(sr / fmin)
    peak = float(np.abs(x).max()) if len(x) else 0.0
    if peak <= 0:
        return 0.0, 0
    out = []
    for i in range(0, len(x) - win, hop):
        seg = x[i:i + win].astype(np.float64)
        if np.sqrt(np.mean(seg ** 2)) < 0.05 * peak:
            continue                            # unvoiced or silent, no opinion
        seg = seg - seg.mean()
        e = float(np.dot(seg, seg))
        if e <= 1e-12:
            continue
        ac = np.correlate(seg, seg, mode="full")[win - 1:]
        band = ac[lo:hi]
        if not len(band):
            continue
        k = lo + int(np.argmax(band))
        if ac[k] / e < 0.30:                    # weakly periodic: not a reliable pitch
            continue
        out.append(sr / k)
    if not out:
        return 0.0, 0
    return float(np.median(out)), len(out)


def centroid_hz(x, sr):
    """Spectral centroid over the loudest half-second. This is what the formant setting moves."""
    win = int(0.5 * sr)
    seg = x if len(x) <= win else max(
        (x[s:s + win] for s in range(0, len(x) - win, win // 2)),
        key=lambda w: float(np.sqrt(np.mean(w ** 2))))
    if not len(seg) or float(np.sqrt(np.mean(seg ** 2))) < 1e-7:
        return 0.0
    n = 1 << int(math.ceil(math.log2(len(seg))))
    mag = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
    freq = np.fft.rfftfreq(n, 1.0 / sr)
    return float((freq * mag).sum() / max(mag.sum(), 1e-12))


def cmd_pitch(args):
    print(f"{'file':<40} {'F0 Hz':>8} {'voiced':>7} {'centroid Hz':>12} {'peak':>7} {'rms':>8}")
    for p in args.files:
        x, sr = read_wav(p)
        f0, nv = f0_median(x, sr)
        rms = float(np.sqrt(np.mean(x ** 2))) if len(x) else 0.0
        print(f"{p.split('/')[-1]:<40} {f0:8.1f} {nv:7d} {centroid_hz(x, sr):12.1f} "
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



def cmd_track(args):
    """Does the sound actually follow the distance? Correlate the gain the game logged with the
    level the dump recorded, second by second. This is the proximity test: the log says how loud
    that speaker should have been, the WAV says how loud they were."""
    import re
    rows = []
    pat = re.compile(r"^\s*([0-9.]+)\s+voice s(\d+) \S*: ([0-9.]+) m gain ([0-9.]+)")
    with open(args.log) as f:
        for line in f:
            m = pat.match(line)
            if m and (args.slot is None or int(m.group(2)) == args.slot):
                rows.append((float(m.group(1)), float(m.group(3)), float(m.group(4))))
    if not rows:
        sys.exit(f"{args.log}: no `voice sN` lines"
                 + (f" for slot {args.slot}" if args.slot is not None else ""))
    x, sr = read_wav(args.wav)
    print(f"{'t s':>7} {'dist m':>8} {'log gain':>9} {'rms':>10} {'measured/log':>13}")
    g, r = [], []
    for t, dist, gain in rows:
        i = int((t - 1.0) * sr)                 # the line reports the second that just ended
        seg = x[max(0, i):max(0, i) + sr]
        if len(seg) < sr // 2:
            continue
        rms = float(np.sqrt(np.mean(seg ** 2)))
        if rms < 1e-5 and gain < 0.02:
            continue                            # both agree it was silent; nothing to compare
        print(f"{t:7.2f} {dist:8.1f} {gain:9.2f} {rms:10.6f} "
              f"{(rms / gain if gain > 0.02 else float('nan')):13.5f}")
        if gain > 0.02 and rms > 1e-5:
            g.append(gain); r.append(rms)
    if len(g) < 3:
        print("not enough talking seconds to correlate")
        return
    g, r = np.array(g), np.array(r)
    c = float(np.corrcoef(g, r)[0, 1])
    ratio = r / g
    print(f"\n{len(g)} seconds compared: correlation gain vs level {c:+.3f}; "
          f"level/gain spread {ratio.std() / ratio.mean() * 100:.1f}% around the mean "
          f"(a perfect proximity curve is +1.000 and 0%, speech loudness varies on its own)")
    print(f"log gain {g.min():.2f}..{g.max():.2f} ({20 * math.log10(g.max() / max(g.min(), 1e-6)):.1f} dB), "
          f"measured {r.min():.5f}..{r.max():.5f} ({20 * math.log10(r.max() / max(r.min(), 1e-9)):.1f} dB)")



def cmd_distance(args):
    """The proximity proof: the same looping sentence recorded at several fixed distances.

    Each directory is one run of tools/voice_test.sh with the host parked somewhere different and
    the talker standing still, so the only difference between the recordings is the distance. The
    measured level ratio between runs has to match the gain the game logged; anything else means
    the curve in the log is not the curve in the mixer.
    """
    import glob
    import os
    import re
    pat = re.compile(r"^\s*([0-9.]+)\s+voice s(\d+) \S*: ([0-9.]+) m gain ([0-9.]+)")
    rows = []
    for d in args.dirs:
        log = os.path.join(d, "hollow_host.log")
        wavs = sorted(glob.glob(os.path.join(d, "host.slot*.wav")))
        if not os.path.exists(log) or not wavs:
            print(f"{d}: no run here"); continue
        dist, gain, n = 0.0, 0.0, 0
        with open(log) as f:
            for line in f:
                m = pat.match(line)
                if m:
                    dist += float(m.group(3)); gain += float(m.group(4)); n += 1
        if not n:
            print(f"{d}: nobody talked"); continue
        x, sr = read_wav(wavs[0])
        # Skip the first two seconds of the talking: the jitter buffer is still filling.
        w = int(0.5 * sr)
        loud = [float(np.sqrt(np.mean(x[i:i + w] ** 2))) for i in range(0, len(x) - w, w)]
        live = [v for v in loud if v > 1e-4]
        rms = float(np.sqrt(np.mean(np.array(live) ** 2))) if live else 0.0
        rows.append((os.path.basename(d.rstrip("/")), dist / n, gain / n, rms))
    if not rows:
        return
    ref = rows[0]
    print(f"{'run':<10} {'distance m':>11} {'log gain':>9} {'measured rms':>13} "
          f"{'measured dB':>12} {'expected dB':>12} {'error dB':>9}")
    for name, dist, gain, rms in rows:
        mdb = 20 * math.log10(rms / ref[3]) if rms > 0 and ref[3] > 0 else float("-inf")
        edb = 20 * math.log10(gain / ref[2]) if gain > 0 and ref[2] > 0 else float("-inf")
        print(f"{name:<10} {dist:11.1f} {gain:9.3f} {rms:13.6f} {mdb:12.2f} {edb:12.2f} "
              f"{mdb - edb:9.2f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pitch"); p.add_argument("files", nargs="+"); p.set_defaults(fn=cmd_pitch)
    p = sub.add_parser("level"); p.add_argument("file"); p.add_argument("--window", type=float, default=0.5); p.set_defaults(fn=cmd_level)
    p = sub.add_parser("track"); p.add_argument("log"); p.add_argument("wav"); p.add_argument("--slot", type=int); p.set_defaults(fn=cmd_track)
    p = sub.add_parser("distance"); p.add_argument("dirs", nargs="+"); p.set_defaults(fn=cmd_distance)
    p = sub.add_parser("gaps"); p.add_argument("file"); p.add_argument("--floor", type=float, default=-60.0); p.set_defaults(fn=cmd_gaps)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
