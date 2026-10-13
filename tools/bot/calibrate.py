#!/usr/bin/env python3
"""calibrate.py -- what the player's moves really cost, from bot action logs.

    python3 tools/bot/calibrate.py RUN            # a backtest run (name or directory)
    python3 tools/bot/calibrate.py actions.jsonl  # one level's log

Reads the action logs (KAROO_BOT_ACTIONS, src/bot/botrun.h) and groups the
moves by what they were: the keys that started them, the tile kinds they left
and reached, the height change and the step time in force.  For each group:
how many, the duration (min, median, max) and how far the player went.  A
group whose durations spread, or whose moves went further than one cell
(ice, slides, teleporters), is where the bot's model of a move needs care.
The gaps line counts the time between one move's end and the next one's
start: time the motor stood still.
"""
import argparse
import glob
import json
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backtest  # noqa: E402

KIND = {0x00: "void", 0x01: "floor", 0x02: "sticky", 0x04: "exit", 0x05: "stairs",
        0x06: "stairs", 0x07: "stairs", 0x08: "stairs", 0x09: "lift", 0x0c: "track",
        0x0d: "falling", 0x0e: "jumppad", 0x0f: "teleport", 0x10: "slide",
        0x11: "switch", 0x12: "bridge", 0x13: "bridge", 0x15: "ice"}


def logs(target):
    if target.endswith(".jsonl"):
        return [target]
    run = backtest.resolve_run(target)
    return sorted(glob.glob(os.path.join(run, "levels", "*", "actions.jsonl")))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("target")
    args = ap.parse_args()

    groups = {}
    gaps = []
    for path in logs(args.target):
        prev_end = None
        with open(path) as f:
            for line in f:
                a = json.loads(line)
                dist = abs(a["to"][0] - a["from"][0]) + abs(a["to"][1] - a["from"][1])
                dh = a["to"][2] - a["from"][2]
                k = (a["keys"], KIND.get(a["kind_from"], "%02x" % a["kind_from"]),
                     KIND.get(a["kind_to"], "%02x" % a["kind_to"]),
                     dh, a["step_ms"], a["reversed"])
                groups.setdefault(k, []).append((a["ms"], dist))
                if prev_end is not None:
                    gaps.append(a["start_ms"] - prev_end)
                prev_end = a["start_ms"] + a["ms"]

    if not groups:
        sys.exit("no moves logged")
    print("%-8s %-8s %-8s %3s %5s %3s %6s  %7s %7s %7s  %s"
          % ("keys", "from", "to", "dh", "step", "rev", "count", "min", "median", "max", "cells"))
    for k in sorted(groups, key=lambda k: -len(groups[k])):
        ms = [m for m, _ in groups[k]]
        dist = sorted(set(d for _, d in groups[k]))
        print("%-8s %-8s %-8s %+3d %5.0f %3d %6d  %7.1f %7.1f %7.1f  %s"
              % (k[0], k[1], k[2], k[3], k[4], k[5], len(ms), min(ms),
                 statistics.median(ms), max(ms), ",".join(map(str, dist))))
    if gaps:
        print("gaps between moves: %d, min %.1f, median %.1f, max %.1f ms"
              % (len(gaps), min(gaps), statistics.median(gaps), max(gaps)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
