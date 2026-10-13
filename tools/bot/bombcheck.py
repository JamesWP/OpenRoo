#!/usr/bin/env python3
"""bombcheck.py -- how often the bomb model predicts where a bomb blows.

    python3 tools/bot/backtest.py run --policy explore --seeds 1,2,3 --label bombs
    python3 tools/bot/bombcheck.py RUN [--examples 5]

Reads the bomb logs (KAROO_BOT_BOMB_LOG, src/bot/botrun.h): each bomb's
blast cell and height as the model (src/bot/bombmodel.h) predicted when the
bomb was first seen, beside the cell and height it was on when its fuse ran
out.  A bomb the model calls lost (rolled off into the void) is right when it
fell below the floor.  Prints the hit rate, and the misses with the level to
look at.
"""
import argparse
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backtest  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("run")
    ap.add_argument("--examples", type=int, default=5)
    args = ap.parse_args()

    run = backtest.resolve_run(args.run)
    n = hit = 0
    misses = []
    for path in sorted(glob.glob(os.path.join(run, "levels", "*", "bombs.jsonl"))):
        with open(os.path.join(os.path.dirname(path), "summary.json")) as f:
            level = json.load(f).get("level", "?")
        with open(path) as f:
            for line in f:
                b = json.loads(line)
                n += 1
                lost = b["actual"][2] < 1  # fell through the floor: no blast to fear
                if b["pred"][:2] == b["actual"][:2] and (
                        (b["pred_lost"] and lost) or (not b["pred_lost"] and b["pred"] == b["actual"])):
                    hit += 1
                else:
                    misses.append("%s: dropped %.0f, seen %.0f ms after: pred %s%s actual %s"
                                  % (level, b["dropped_at"], b["seen_after_ms"], b["pred"],
                                     " (lost)" if b["pred_lost"] else "", b["actual"]))
    if not n:
        sys.exit("no bomb logs in %s" % run)
    print("bomb model right on %d of %d blasts (%.1f%%)" % (hit, n, 100.0 * hit / n))
    for m in misses[:args.examples]:
        print("  " + m)
    return 0


if __name__ == "__main__":
    sys.exit(main())
