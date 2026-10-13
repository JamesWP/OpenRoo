#!/usr/bin/env python3
"""showlevel.py -- print a level's grid from a bot trace, as the bot saw it.

    python3 tools/bot/showlevel.py TRACE.jsonl [--frame N] [--field height|kind|contents]

TRACE is a KAROO_BOT_TRACE file (backtest.py run --trace keeps one per level),
or a backtest level directory holding trace.jsonl.  The grid is the first
line's, with the changes up to --frame applied (default: the first frame).
v runs down the page, u across; each cell is two characters:

    height   the tile height, '..' for no tile, ## a wall, BB a bombable block
    kind     a letter per tile kind (the legend is printed)
    contents a letter per pickup

and over it  P the player (with its facing ^ > v <), F a foe, X the exit.
"""
import argparse
import json
import os
import sys

KINDS = {0x00: "..", 0x01: "  ", 0x02: "st", 0x04: "EX", 0x05: "S^", 0x06: "S>",
         0x07: "Sv", 0x08: "S<", 0x09: "LI", 0x0a: "PU", 0x0b: "PV", 0x0c: "pt",
         0x0d: "FT", 0x0e: "JP", 0x0f: "TP", 0x10: "SL", 0x11: "SW", 0x12: "BU",
         0x13: "BV", 0x15: "IC", 0x16: "##", 0x17: "BB"}
CONTENTS = {0x01: "**", 0x05: "pg", 0x06: "tm", 0x07: "1U", 0x08: "fz", 0x09: "bm",
            0x0a: "sp", 0x0b: "rv", 0x0c: "sl", 0x0d: "pr", 0xff: "??"}
FACING = {1: "^", 2: ">", 3: "v", 4: "<"}


def load(path, frame):
    if os.path.isdir(path):
        path = os.path.join(path, "trace.jsonl")
    with open(path) as f:
        first = json.loads(f.readline())
        fields = first["cell_fields"]
        cells = {(c[0], c[1]): dict(zip(fields, c)) for c in first["cells"]}
        rec = first
        if frame is not None:
            for line in f:
                r = json.loads(line)
                if r["frame"] > frame:
                    break
                for c in r.get("changed", []):
                    cells[(c[0], c[1])] = dict(zip(fields, c))
                rec = r
    return first, rec, cells


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("trace")
    ap.add_argument("--frame", type=int)
    ap.add_argument("--field", default="height", choices=["height", "kind", "contents"])
    args = ap.parse_args()

    first, rec, cells = load(args.trace, args.frame)
    eu, ev = first["extent"]
    over = {}
    for f in rec["foes"]:
        over[(f["cell"][0], f["cell"][1])] = "F" + FACING.get(f["facing"], "?")
    ex = first["exit"]
    over[(ex[0], ex[1])] = "X "
    p = rec["player"]
    over[(p["cell"][0], p["cell"][1])] = "P" + FACING.get(p["facing"], "?")

    print("%s  frame %d  (u across, v down)" % (first["level"], rec["frame"]))
    print("    " + "".join("%2d" % (u % 100) for u in range(eu)))
    for v in range(ev):
        row = []
        for u in range(eu):
            c = cells[(u, v)]
            if (u, v) in over:
                row.append(over[(u, v)])
            elif args.field == "height":
                if c["kind"] in (0x16, 0x17):  # impassable, bombable
                    row.append(KINDS[c["kind"]])
                else:
                    row.append("%2d" % c["height"] if c["height"] else "..")
            elif args.field == "kind":
                row.append(KINDS.get(c["kind"], "%02x" % c["kind"]))
            else:
                row.append(CONTENTS.get(c["contents"], "%02x" % c["contents"])
                           if c["contents"] else "  ")
        print("%3d " % v + "".join(row))
    if args.field == "kind":
        print("kinds: " + "  ".join("%s=%02x" % (k, n) for n, k in sorted(KINDS.items())))
    if args.field == "contents":
        print("contents: " + "  ".join("%s=%02x" % (k, n) for n, k in sorted(CONTENTS.items())))


if __name__ == "__main__":
    sys.exit(main())
