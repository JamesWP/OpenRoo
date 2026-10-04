#!/usr/bin/env python3
"""Draw-call regression check: collect KAROO_DRAW_TRACE over replays.

    python3 tools/drawtrace.py collect DIR                # every recording
    python3 tools/drawtrace.py collect DIR NAME [NAME...]
    python3 tools/drawtrace.py diff DIR_A DIR_B           # exit 1 if they differ

Each draw the game makes is hashed together with the pipeline state the game
has set for it (src/gl/drawtrace.cpp), one line per draw.  Two builds
that draw the same pictures leave identical traces, so diffing the traces of a
replay before and after a change to the rendering code shows whether it moved
a draw, a vertex or a state -- however differently the code now gets there.

The replays run against the real device (--no-headless, which needs a display)
and render every frame (--no-fast); a headless or fast run reaches no draw.
A few draws can differ between two runs of the very same build (two of the
sixteen recordings have one each); compare the number of differing draws, and
run the "before" build twice when it matters.  Build the "before" side from a clean checkout: launch.sh rebuilds the tree it
is in, so editing sources mid-collect changes what the run measures.
"""
import argparse, json, os, subprocess, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def recordings():
    with open(os.path.join(REPO, "tests", "manifest.json")) as fh:
        return [r["name"] for r in json.load(fh)["recordings"]]


def collect(out, names):
    os.makedirs(out, exist_ok=True)
    for n in names:
        path = os.path.abspath(os.path.join(out, n + ".trace"))
        # Tracing and rendering for real is slow; the stuck detector assumes a
        # plain run.
        env = dict(os.environ, KAROO_DRAW_TRACE=path,
                   KAROO_STUCK_SECONDS=os.environ.get("KAROO_STUCK_SECONDS", "240"))
        cmd = [sys.executable, os.path.join(REPO, "tools", "replaytest.py"),
               "--no-headless", "--no-fast", n]
        r = subprocess.run(cmd, env=env, capture_output=True, text=True)
        verdict = "pass" if r.returncode == 0 else "FAIL"
        draws = sum(1 for _ in open(path)) if os.path.exists(path) else 0
        print("%-20s %s, %d draws" % (n, verdict, draws), flush=True)


def diff(a, b):
    same = bad = 0
    for f in sorted(os.listdir(a)):
        if not f.endswith(".trace"):
            continue
        pa, pb = os.path.join(a, f), os.path.join(b, f)
        if not os.path.exists(pb):
            print("MISSING   %s" % f)
            bad += 1
            continue
        with open(pa) as x, open(pb) as y:
            first, n = None, 0
            for i, (lx, ly) in enumerate(zip(x, y)):
                if lx != ly:
                    n += 1
                    if first is None:
                        first = (i, lx.rstrip("\n"), ly.rstrip("\n"))
        nx, ny = sum(1 for _ in open(pa)), sum(1 for _ in open(pb))
        if nx != ny:
            print("DIFFERENT %s: %d draws vs %d" % (f, nx, ny))
            bad += 1
        elif first:
            print("DIFFERENT %s: %d of %d draws; first is draw %d\n  - %s\n  + %s"
                  % (f, n, nx, first[0], first[1], first[2]))
            bad += 1
        else:
            print("identical %s (%d draws)" % (f, nx))
            same += 1
    print("%d identical, %d different" % (same, bad))
    return bad == 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("collect")
    c.add_argument("dir")
    c.add_argument("names", nargs="*")
    d = sub.add_parser("diff")
    d.add_argument("a")
    d.add_argument("b")
    a = ap.parse_args()
    if a.cmd == "collect":
        collect(a.dir, a.names or recordings())
    else:
        sys.exit(0 if diff(a.a, a.b) else 1)


main()
