#!/usr/bin/env python3
"""genlevels.py -- generated levels to test the foe model on.

    python3 tools/bot/genlevels.py OUT [--count 40] [--first 0] [--seed 1]
    python3 tools/bot/backtest.py run --policy explore --overlay OUT --levels 0-39 --label gen
    python3 tools/bot/foecheck.py RUN

Writes OUT laid out as game/ (backtest.py --overlay): the maps of levels
first .. first + count - 1 replaced by generated ones, each keeping its own
trailer (theme, title) but with a long time limit and a quota of every gem.

Each is a 20 x 20 map of two flat areas at different heights, holes and
walls in them, a thrower, the player's start, an exit, a few gems, and one
or more teleporter pairs on the thrower's way to the player: the areas
joined by a walkway as well, by the teleporters alone, or a pair within one
area.  The foe model is to send a thrower through a pad only where its path
to the player steps onto one (movableentity.cpp), as the game does.

Map format: src/level/levelmap.cpp (cells of height, kind, param, contents).
"""
import argparse
import os
import random
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import karoosave  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME = os.path.join(REPO, "game")

N = 20
PLAIN, START, EXIT, TELEPORTER = 0x01, 0x03, 0x04, 0x0f
CRYSTAL, THROWER = 0x01, 0x03
TIME_LIMIT = 900


def flood(grid, start):
    """The cells reached from start by steps between cells of one height."""
    seen = {start}
    open_ = [start]
    while open_:
        u, v = open_.pop()
        for du, dv in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            c = (u + du, v + dv)
            if c not in seen and 0 < c[0] < N - 1 and 0 < c[1] < N - 1 and \
                    grid[c[1]][c[0]][0] == grid[v][u][0] and grid[c[1]][c[0]][0] > 0:
                seen.add(c)
                open_.append(c)
    return seen


def generate(rng):
    """One level's cells, rows of [height, kind, param, contents], and its gem count."""
    while True:
        grid = [[[0, 0, 0, 0] for _ in range(N)] for _ in range(N)]
        mode = rng.choice(("walkway", "teleport", "within"))
        split = rng.randint(7, 12)            # area A: v < split, B: v > split
        ha = 40
        hb = rng.choice((30, 35)) if mode == "teleport" else ha
        for v in range(1, N - 1):
            for u in range(1, N - 1):
                if v < split:
                    grid[v][u] = [ha, PLAIN, 0, 0]
                elif v > split:
                    grid[v][u] = [hb, PLAIN, 0, 0]
        if mode != "teleport":
            # A walkway across the gap, at a height the foes step along.
            u = rng.randint(2, N - 3)
            grid[split][u] = [ha, PLAIN, 0, 0]
        # Holes and walls.
        for _ in range(rng.randint(8, 30)):
            u, v = rng.randint(1, N - 2), rng.randint(1, N - 2)
            if v == split:
                continue
            if rng.random() < 0.6:
                grid[v][u] = [0, 0, 0, 0]
            else:
                grid[v][u] = [grid[v][u][0] + 5, PLAIN, 0, 0] if grid[v][u][0] else grid[v][u]
        cells = lambda area: [(u, v) for v in range(1, N - 1) for u in range(1, N - 1)
                              if (v < split if area == 0 else v > split) and grid[v][u][0] in (ha, hb)
                              and grid[v][u][1] == PLAIN and grid[v][u][3] == 0]
        a, b = cells(0), cells(1)
        if len(a) < 30 or len(b) < 30:
            continue
        rng.shuffle(a)
        rng.shuffle(b)
        thrower = a.pop()
        start = b.pop() if rng.random() < 0.7 else a.pop()
        # The pads: one by the thrower's side, one by the player's, or both
        # in one area for a shortcut.
        pairs = rng.randint(1, 2)
        pads = []
        for k in range(pairs):
            if mode == "within" or rng.random() < 0.2:
                area = rng.choice((a, b))
                pads.append((area.pop(), area.pop()))
            else:
                pads.append((a.pop(), b.pop()))
        exit_ = rng.choice((a, b)).pop()
        gems = [rng.choice((a, b)).pop() for _ in range(rng.randint(2, 5))]
        grid[thrower[1]][thrower[0]][3] = THROWER
        grid[start[1]][start[0]][1] = START
        grid[exit_[1]][exit_[0]][1] = EXIT
        for (u, v) in gems:
            grid[v][u][3] = CRYSTAL
        for k, (p, q) in enumerate(pads):
            for (u, v) in (p, q):
                grid[v][u][1] = TELEPORTER
                grid[v][u][2] = k + 1
        # Each pad's cell is on its area (not walled off) and the thrower and
        # the player stand on ground of their area.
        if any(len(flood(grid, c)) < 10 for c in (thrower, start)):
            continue
        return grid, len(gems)


def real_path(rel):
    """rel under game/ as the files are named: the game's names ignore case
    (sysdev::nativePath)."""
    at = GAME
    parts = []
    for part in rel.split(os.sep):
        match = next((n for n in os.listdir(at) if n.lower() == part.lower()), part)
        parts.append(match)
        at = os.path.join(at, match)
    return os.path.join(*parts)


def write(path, grid, gems, trailer):
    out = bytearray([N, N])
    for v in range(N):
        for u in range(N):
            out += bytes(grid[v][u])
    out += struct.pack("<ii", gems, TIME_LIMIT) + trailer[8:]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out")
    ap.add_argument("--count", type=int, default=40)
    ap.add_argument("--first", type=int, default=0)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    names = karoosave.load_levels(os.path.join(GAME, "JJ.GAM"))
    rng = random.Random(args.seed)
    for i in range(args.first, min(args.first + args.count, len(names))):
        rel = real_path(os.path.join("Levels", *names[i].split("\\")) + ".jjm")
        with open(os.path.join(GAME, rel), "rb") as f:
            d = f.read()
        trailer = d[2 + d[0] * d[1] * 4:]
        grid, gems = generate(rng)
        write(os.path.join(args.out, rel), grid, gems, trailer)
    print("%d levels -> %s" % (min(args.count, len(names) - args.first), args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
