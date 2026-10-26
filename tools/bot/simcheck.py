#!/usr/bin/env python3
"""simcheck.py -- how much of the world the world sim gets right.

    python3 tools/bot/backtest.py run --label sim
    python3 tools/bot/simcheck.py RUN [--examples 3] [--levels]

Reads the sim logs (KAROO_BOT_SIM_LOG, src/bot/checks.h): the world sim
(src/botworld/worldsim.h) run from the view some ticks ago with the presses
really held since, against the view now, part by part (compare.h).  Prints,
for each part and look-ahead, how often it was right; with --levels, the
worst levels for each part; and a few examples of each part's misses.
"""
import argparse
import collections
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backtest  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("run")
    ap.add_argument("--examples", type=int, default=3)
    ap.add_argument("--levels", action="store_true", help="the worst levels for each part")
    args = ap.parse_args()

    run = backtest.resolve_run(args.run)
    by = collections.defaultdict(lambda: [0, 0])          # (part, ahead) -> right, n
    by_level = collections.defaultdict(lambda: [0, 0])    # (part, level) -> right, n
    misses = collections.defaultdict(list)                # part -> examples
    aheads = set()
    clock_off = 0
    paths = sorted(glob.glob(os.path.join(run, "levels", "*", "sim.jsonl")))
    if not paths:
        sys.exit("no sim logs in %s (a run from before KAROO_BOT_SIM_LOG?)" % run)
    for path in paths:
        with open(os.path.join(os.path.dirname(path), "summary.json")) as f:
            level = json.load(f).get("level", "?")
        with open(path) as f:
            for line in f:
                r = json.loads(line)
                ahead = r["ahead"]
                aheads.add(ahead)
                clock_off += abs(r["clock_ms"]) > 0.5
                for part, right in r["parts"].items():
                    by[part, ahead][0] += right
                    by[part, ahead][1] += 1
                    by_level[part, level][0] += right
                    by_level[part, level][1] += 1
                    if not right and len(misses[part]) < args.examples:
                        misses[part].append("%s frame %d +%d: %s" % (
                            level, r["frame"], ahead, r["why"].get(part, "")))

    aheads = sorted(aheads)
    parts = sorted({p for p, _ in by}, key=lambda p: min(
        by[p, a][0] / by[p, a][1] for a in aheads if by[p, a][1]))
    print("%-15s" % "part" + "".join("%14s" % ("+%d ticks" % a) for a in aheads))
    for part in parts:
        row = "%-15s" % part
        for a in aheads:
            right, n = by[part, a]
            row += "%14s" % ("%5.1f%% %6d" % (100.0 * right / n, n) if n else "-")
        print(row)
    if clock_off:
        print("\n%d checks ended off the view's clock" % clock_off)

    if args.levels:
        print("\nworst levels")
        for part in parts:
            rows = sorted(((r / n, lv, n) for (p, lv), (r, n) in by_level.items() if p == part and n))
            print("  %-15s " % part + ", ".join("%s %.0f%%" % (lv, 100 * x) for x, lv, _ in rows[:5]))

    if args.examples:
        print("\nmisses")
        for part in parts:
            for m in misses[part]:
                print("  %-15s %s" % (part, m))


if __name__ == "__main__":
    main()
