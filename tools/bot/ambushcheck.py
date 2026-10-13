#!/usr/bin/env python3
"""ambushcheck.py -- did the foes die where and when the bot said they would?

    python3 tools/bot/backtest.py run --policy plan --trace --levels 9
    python3 tools/bot/ambushcheck.py RUN [--level 9]

Reads each level's decisions.jsonl (an ambush decision logs predicted_kills:
[ms, u, v] for each foe its bomb is to kill, ms on the game's clock) and the
trace (the foes' cells each tick, and when one starts dying), and prints each
ambush with the kills predicted beside those that happened in the blast's
window: the bomb's drop to 2.6 s after.  A predicted kill is right when a foe
dies within one cell and 200 ms of it.
"""
import argparse
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backtest  # noqa: E402


def deaths(trace):
    """(ms, u, v) for each foe the trace sees begin to die."""
    out = []
    seen = set()
    with open(trace) as f:
        for line in f:
            r = json.loads(line)
            for foe in r["foes"]:
                if foe["dying"]:
                    key = (foe["cell"][0], foe["cell"][1], int(r["now"] // 3000))
                    if key not in seen:
                        seen.add(key)
                        out.append((r["now"], foe["cell"][0], foe["cell"][1]))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("run")
    ap.add_argument("--level", type=int)
    args = ap.parse_args()
    run = backtest.resolve_run(args.run)
    right = wrong = 0
    for d in sorted(glob.glob(os.path.join(run, "levels", "*"))):
        trace = os.path.join(d, "trace.jsonl")
        log = os.path.join(d, "decisions.jsonl")
        if not os.path.exists(trace) or not os.path.exists(log):
            continue
        if args.level is not None and int(os.path.basename(d).split("-")[0]) != args.level:
            continue
        dead = deaths(trace)
        for line in open(log):
            j = json.loads(line)
            if not j.get("predicted_kills"):
                continue
            window = [x for x in dead if j["now"] <= x[0] <= j["now"] + 6000]
            for pms, pu, pv in j["predicted_kills"]:
                hit = [x for x in window if abs(x[1] - pu) <= 1 and abs(x[2] - pv) <= 1
                       and abs(x[0] - pms) <= 200]
                right += bool(hit)
                wrong += not hit
                print("%s decision at %.0f: predicted (%d,%d) at %.0f  %s  actual %s"
                      % (os.path.basename(d), j["now"], pu, pv, pms,
                         "ok " if hit else "MISS",
                         ", ".join("(%d,%d) at %.0f" % (u, v, t) for t, u, v in window) or "none"))
    print("%d of %d predicted kills happened" % (right, right + wrong))
    return 0


if __name__ == "__main__":
    sys.exit(main())
