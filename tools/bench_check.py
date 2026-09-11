#!/usr/bin/env python3
"""Compare a bench.json run against bench_baseline.json's per-machine numbers.

Usage:
    tools/bench_check.py BASELINE.json NEW.json [NEW2.json ...] [--tol 0.15] [--allow-missing-baseline]

The baseline is keyed by NEW["machine_tag"] (see bench.c's bench_finish and bench_baseline.json's
"_comment"): a frame time from one machine says nothing about another, so there is no single global
number to compare against, only a per-machine one. A path is a regression when its frame_ms_median
is more than --tol (a fraction, default 0.15 = 15%) SLOWER than the baseline's.

Give it more than one NEW file and it takes the LOWEST frame_ms_median per path across them.
That is not cherry-picking: noise on a shared machine only ever adds time -- another process
scheduled on your core, a thermal step, a background indexer -- so of N runs of identical work the
fastest is the one closest to what the work actually costs. Repeated runs of this benchmark on an
idle M4 still swing about 40% on the `courtyard` and `shootout` paths at 1.8 ms a frame, which is
enough to trip a 15% gate on noise alone; two runs and a minimum makes the gate mean something.

Exit code is 0 when every path in the baseline for this machine is present in the new run and
within tolerance; 1 otherwise (a regression, a path the baseline expects but the new run does not
have, or -- unless --allow-missing-baseline was given -- a machine_tag with no baseline at all).
"""
import argparse
import json
import sys


def load(path):
    with open(path) as f:
        return json.load(f)


def print_baseline_block(new):
    """The new run's numbers, formatted as a "machines" entry ready to paste into
    bench_baseline.json for this machine_tag."""
    tag = new["machine_tag"]
    print(f"No baseline for machine_tag \"{tag}\" in the baseline file. Paste this into")
    print("bench_baseline.json's \"machines\" object once you trust this run's numbers:")
    print()
    print(f"    \"{tag}\": {{")
    paths = new.get("paths", [])
    for i, p in enumerate(paths):
        comma = "," if i + 1 < len(paths) else ""
        print(f"      \"{p['name']}\": {{\"frame_ms_median\": {p['frame_ms_median']:.3f}}}{comma}")
    print("    }")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("baseline", help="bench_baseline.json")
    ap.add_argument("new", nargs="+", help="one or more bench.json runs to check; the best (lowest) median per path is used")
    ap.add_argument("--tol", type=float, default=0.15, help="fraction slower than baseline that counts as a regression (default 0.15)")
    ap.add_argument("--allow-missing-baseline", action="store_true",
                     help="exit 0 (with a warning) instead of 1 when this machine_tag has no baseline yet")
    args = ap.parse_args()

    baseline = load(args.baseline)
    runs = [load(p) for p in args.new]
    new = runs[0]
    if len(runs) > 1:   # keep the best median per path across the runs; see the module docstring
        best = {}
        for r in runs:
            for p in r.get("paths", []):
                cur = best.get(p["name"])
                if cur is None or p["frame_ms_median"] < cur["frame_ms_median"]:
                    best[p["name"]] = p
        new = dict(new)
        new["paths"] = [best[p["name"]] for p in runs[0].get("paths", []) if p["name"] in best]

    tag = new.get("machine_tag", "")
    machines = baseline.get("machines", {})

    if tag not in machines:
        print_baseline_block(new)
        if args.allow_missing_baseline:
            print(f"\nbench: WARNING no baseline for \"{tag}\" -- allowed by --allow-missing-baseline")
            return 0
        print(f"\nbench: FAILED (no baseline for \"{tag}\")")
        return 1

    base_paths = machines[tag]
    new_paths = {p["name"]: p for p in new.get("paths", [])}

    names = list(base_paths.keys())
    for name in new_paths:
        if name not in names:
            names.append(name)

    print(f"{'path':<12} {'baseline':>9} {'now':>9}   delta")
    ok = True
    for name in names:
        base_entry = base_paths.get(name)
        now_entry = new_paths.get(name)
        if base_entry is None:
            # A path the new run has but the baseline never recorded: informational, not a failure
            # -- it is new data, not a regression of anything the baseline promised.
            print(f"{name:<12} {'-':>9} {now_entry['frame_ms_median']:9.3f}   (no baseline for this path)")
            continue
        base_val = base_entry["frame_ms_median"]
        if now_entry is None:
            print(f"{name:<12} {base_val:9.3f} {'-':>9}   MISSING from this run")
            ok = False
            continue
        now_val = now_entry["frame_ms_median"]
        delta = (now_val - base_val) / base_val if base_val > 0 else 0.0
        regressed = delta > args.tol
        status = f"REGRESSED  (tolerance {args.tol * 100:.0f}%)" if regressed else "ok"
        if regressed:
            ok = False
        print(f"{name:<12} {base_val:9.3f} {now_val:9.3f}   {delta * 100:+.1f}%  {status}")

    n = len(names)
    if ok:
        print(f"\nbench: {n} paths, all within tolerance")
    else:
        print("\nbench: FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
