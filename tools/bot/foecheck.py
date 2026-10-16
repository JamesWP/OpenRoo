#!/usr/bin/env python3
"""foecheck.py -- how often the foe model predicts where the foes go.

    python3 tools/bot/backtest.py run --policy explore --seeds 1,2 --label foes
    python3 tools/bot/foecheck.py RUN [--examples 3]

Reads the foe logs (KAROO_BOT_FOE_LOG, src/bot/botrun.h): the foe model
(src/bot/foemodel.h) run from the view some time ago, with the player where it
really went since, against where each foe really is.  Prints the hit rate by
look-ahead, then by foe type and kind, then by level, and a baseline beside
each: how often "the foe stays where it was" would have been right, so a
rate can be read against how much the foes moved.  Then the foes' deaths: of
the look-aheads over which some foe died or the model said one would, how
often the model left the right number alive, by level.
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
    args = ap.parse_args()

    run = backtest.resolve_run(args.run)
    by = collections.defaultdict(lambda: [0, 0, 0])   # key -> hits, stays, n
    misses = collections.defaultdict(list)
    deaths = collections.defaultdict(lambda: [0, 0])  # (level, ahead) -> right, n
    death_misses = collections.defaultdict(list)
    for path in sorted(glob.glob(os.path.join(run, "levels", "*", "foes.jsonl"))):
        with open(os.path.join(os.path.dirname(path), "summary.json")) as f:
            level = json.load(f).get("level", "?")
        with open(path) as f:
            for line in f:
                r = json.loads(line)
                ahead = "%4.0f ms" % r["ahead_ms"]
                if "kill" in r:
                    continue
                if "count" in r:
                    c = r["count"]
                    if c["pred"] != c["was"] or c["actual"] != c["was"]:
                        d = deaths[(level, ahead)]
                        d[0] += c["pred"] == c["actual"]
                        d[1] += 1
                        if c["pred"] != c["actual"]:
                            death_misses[(level, ahead)].append(
                                "frame %d: %d foes, pred %d actual %d, %d bombs dropped"
                                % (r["frame"], c["was"], c["pred"], c["actual"], c["dropped"]))
                    continue
                for f in r["foes"]:
                    hit = f["pred"] == f["actual"]
                    stay = f["was"] == f["actual"]
                    for key in (("ahead", ahead), ("type", "kind %d type %d, %s" % (f["kind"], f["type"], ahead)),
                                ("level", "%s, %s" % (level, ahead))):
                        b = by[key]
                        b[0] += hit
                        b[1] += stay
                        b[2] += 1
                    if not hit:
                        misses[(level, ahead)].append("frame %d: was %s pred %s actual %s"
                                                      % (r["frame"], f["was"], f["pred"], f["actual"]))
    if not by:
        sys.exit("no foe logs in %s" % run)
    for section in ("ahead", "type", "level"):
        print("by %s:%s" % (section, " " * 40 + "sim     stays   (n)"))
        for key in sorted(k for k in by if k[0] == section):
            h, s, n = by[key]
            print("  %-50s %5.1f%%  %5.1f%%  (%d)" % (key[1][:50], 100.0 * h / n, 100.0 * s / n, n))
    print("deaths, by level:%s" % (" " * 31 + "right   (n)"))
    for key in sorted(deaths):
        right, n = deaths[key]
        print("  %-50s %5.1f%%  (%d)" % (("%s, %s" % key)[:50], 100.0 * right / n, n))
    if args.examples:
        for key in sorted(death_misses, key=lambda k: -len(death_misses[k]))[:8]:
            print("  death misses %s, %s: %d" % (key[0], key[1], len(death_misses[key])))
            for e in death_misses[key][:args.examples]:
                print("    " + e)
        print("misses:")
        for key in sorted(misses, key=lambda k: -len(misses[k]))[:8]:
            print("  %s, %s: %d" % (key[0], key[1], len(misses[key])))
            for e in misses[key][:args.examples]:
                print("    " + e)
    return 0


if __name__ == "__main__":
    sys.exit(main())
