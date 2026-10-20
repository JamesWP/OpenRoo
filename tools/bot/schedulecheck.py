#!/usr/bin/env python3
"""schedulecheck.py -- does the player move when the bot's schedule says?

    python3 tools/bot/backtest.py run --policy plan --label plan
    python3 tools/bot/schedulecheck.py RUN [--examples 3]

Reads each level's decisions.jsonl (each decision logs the chosen option's
schedule: the cells the player is to enter, each with the game-clock ms its
step into it starts) and actions.jsonl (each move the game made: its start,
the cell it left and the cell it came to rest on).  The danger check runs
the foes and the blasts against that schedule, so a step that starts a tick
late can walk into a blast the check passed.

A decision's schedule is followed until the next decision.  Each step of it
is matched, in order, to the game's next move out of the same cell; a move
that carries the player on through cells the schedule also lists (a slide,
ice) is matched to the first of them.  For each match: how many ticks late
the move started.  Prints that by what came before the step (the first step
of the schedule, a step after a step, after a turn, after a wait or a
fall), then the steps never taken, with a few examples each.
"""
import argparse
import collections
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import backtest  # noqa: E402

TICK_MS = 1000.0 / 60.0


def load(path):
    with open(path) as f:
        return [json.loads(line) for line in f if line.strip()]


def check_level(decisions, actions, level, out, missed):
    """Adds each scheduled step's lateness, in ticks, to out[context]."""
    for i, d in enumerate(decisions):
        sched = d.get("schedule") or []
        if len(sched) < 2:
            continue
        until = decisions[i + 1]["now"] if i + 1 < len(decisions) else float("inf")
        moves = [a for a in actions if d["now"] - 1 <= a["start_ms"] < until]
        k = 0               # the next move to match
        prev = sched[0]
        prev_t = None       # the last step's scheduled start
        first = True
        j = 1
        while j < len(sched):
            t, u, v = sched[j]
            if [u, v] == prev[1:]:
                prev = sched[j]
                j += 1
                continue
            # The game's next move out of the cell, turns before it skipped.
            while k < len(moves) and (moves[k]["from"][:2] != prev[1:] or
                                      moves[k]["to"][:2] == prev[1:]):
                k += 1
            if k == len(moves):
                if t < until:
                    missed[level].append("decision at %.0f: step (%d,%d)->(%d,%d) at %.0f not taken"
                                         % (d["now"], prev[1], prev[2], u, v, t))
                break
            m = moves[k]
            late = round((m["start_ms"] - t) / TICK_MS)
            if first:
                ctx = "first step"
            else:
                gap = t - prev_t
                step = m.get("step_ms", 200)
                ctx = ("after a step" if abs(gap - step) < 1 else
                       "after a turn" if abs(gap - 2 * step) < 1 else
                       "after a wait or fall")
            ctx += ", " + m["keys"]
            out[ctx][late].append("%s: decision at %.0f, (%d,%d)->(%d,%d) scheduled %.1f, started %.1f"
                                  % (level, d["now"], prev[1], prev[2], u, v, t, m["start_ms"]))
            first = False
            prev_t = t
            # A move carried on past the cell: on to where it came to rest.
            rest = m["to"][:2]
            prev = sched[j]
            while j + 1 < len(sched) and sched[j][1:] != rest:
                j += 1
                prev = sched[j]
            k += 1
            j += 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("run")
    ap.add_argument("--examples", type=int, default=3)
    args = ap.parse_args()

    run = backtest.resolve_run(args.run)
    out = collections.defaultdict(lambda: collections.defaultdict(list))
    missed = collections.defaultdict(list)
    for path in sorted(glob.glob(os.path.join(run, "levels", "*", "decisions.jsonl"))):
        here = os.path.dirname(path)
        actions = os.path.join(here, "actions.jsonl")
        if not os.path.exists(actions):
            continue
        with open(os.path.join(here, "summary.json")) as f:
            level = json.load(f).get("level", "?")
        check_level(load(path), load(actions), level, out, missed)

    total = sum(len(e) for c in out.values() for e in c.values())
    right = sum(len(c.get(0, [])) for c in out.values())
    print("steps started on the scheduled tick: %d of %d (%.1f%%)"
          % (right, total, 100.0 * right / total if total else 0))
    print("ticks late, by what came before the step:")
    for ctx in sorted(out, key=lambda c: -sum(len(e) for e in out[c].values())):
        c = out[ctx]
        n = sum(len(e) for e in c.values())
        spread = "  ".join("%+d:%d" % (late, len(c[late])) for late in sorted(c))
        print("  %-36s %6d   %s" % (ctx, n, spread))
    if args.examples:
        for ctx in sorted(out):
            for late in sorted(out[ctx]):
                if late == 0:
                    continue
                print("%s, %+d ticks:" % (ctx, late))
                for e in out[ctx][late][:args.examples]:
                    print("    " + e)
    n = sum(len(m) for m in missed.values())
    print("steps never taken (another decision first does not count): %d" % n)
    for level in sorted(missed, key=lambda l: -len(missed[l])):
        print("  %s: %d" % (level, len(missed[level])))
        for e in missed[level][:args.examples]:
            print("    " + e)


if __name__ == "__main__":
    main()
