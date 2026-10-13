#!/usr/bin/env python3
"""modelcheck.py -- how often the bot's nav model predicts the game right.

    python3 tools/bot/backtest.py run --policy explore --seeds 1,2,3,4 --label explore
    python3 tools/bot/modelcheck.py RUN [--examples 3]

Reads the model logs (KAROO_BOT_MODEL_LOG, written by the explore policy,
src/bot/explore.h): for each single move made from rest, the nav model's
prediction (src/bot/nav.h) and what the game did.  A prediction is right when
the kinds agree (rest, blocked, death) and a rest is on the cell and facing
the game came to rest on.

Prints the hit rate per move kind, then the mismatches grouped by what the
model said, what happened and the tiles involved, the biggest groups first,
with a few examples each (level, cell, facing) to go and look at: the top
group is the model's most common wrong rule.
"""
import argparse
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backtest  # noqa: E402
import calibrate  # noqa: E402  -- the tile kind names


def kind(k):
    return calibrate.KIND.get(k, "%02x" % k if k >= 0 else "out")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("run")
    ap.add_argument("--examples", type=int, default=3)
    args = ap.parse_args()

    run = backtest.resolve_run(args.run)
    total = right = cut = others = 0
    by_act = {}
    groups = {}
    for path in sorted(glob.glob(os.path.join(run, "levels", "*", "model.jsonl"))):
        level_dir = os.path.dirname(path)
        with open(os.path.join(level_dir, "summary.json")) as f:
            level = json.load(f).get("level", "?")
        with open(path) as f:
            for line in f:
                m = json.loads(line)
                p, a = m["pred"], m["actual"]
                if a["kind"] == "cut":
                    cut += 1
                    continue
                if a["cause"] in ("foe", "bomb", "timeout"):
                    others += 1  # not the terrain's doing
                    continue
                total += 1
                ok = p["kind"] == a["kind"] and (
                    p["kind"] != "rest" or
                    (p["to"][:2] == a["to"][:2] and p["to"][2] == a["to"][3]))
                hit = by_act.setdefault(m["act"], [0, 0])
                hit[1] += 1
                if ok:
                    right += 1
                    hit[0] += 1
                    continue
                key = (m["act"], "%s%s" % (p["kind"], ":" + p["why"] if p["why"] else ""),
                       a["kind"], kind(m["from_kind"]), kind(a["to_kind"]),
                       a["to"][2] - m["from"][2] if a["kind"] == "rest" else None)
                groups.setdefault(key, []).append(
                    "%s (%d,%d,h%d,f%d)->(%d,%d,h%d,f%d) pred (%d,%d,f%d) %.0fms/%.0fms" % (
                        level, *m["from"], *a["to"], *p["to"], p["ms"], a["ms"]))

    if not total:
        sys.exit("no model logs in %s (run the explore policy)" % run)
    print("model right on %d of %d moves (%.1f%%); %d cut off, %d ended by a foe, bomb or the clock"
          % (right, total, 100.0 * right / total, cut, others))
    for act, (h, n) in sorted(by_act.items()):
        print("  %-8s %5d/%-5d %.1f%%" % (act, h, n, 100.0 * h / n))
    print()
    print("%-8s %-18s %-8s %-9s %-9s %4s %6s" % ("act", "predicted", "actual", "from", "to", "dh", "count"))
    for key in sorted(groups, key=lambda k: -len(groups[k])):
        ex = groups[key]
        print("%-8s %-18s %-8s %-9s %-9s %4s %6d" % (
            key[0], key[1], key[2], key[3], key[4],
            "" if key[5] is None else "%+d" % key[5], len(ex)))
        for e in ex[:args.examples]:
            print("    " + e)
    return 0


if __name__ == "__main__":
    sys.exit(main())
