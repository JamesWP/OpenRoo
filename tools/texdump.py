#!/usr/bin/env python3
"""Texture-pixel regression check: collect KAROO_TEXTURE_DUMP over replays.

    python3 tools/texdump.py OUT.txt                       # all recordings, 1024x768x32
    python3 tools/texdump.py OUT.txt --mode 10             # 800x600x16 display
    python3 tools/texdump.py OUT.txt --no-fast             # include the loading screens
    python3 tools/texdump.py --diff A.txt B.txt            # exit 1 if they differ

Every texture and image surface the loaders produce is hashed (name, size,
pixel format, FNV-1a of the pixel bytes).  OUT.txt holds the sorted unique
lines; diff two of them to see whether a loader change moved a pixel.  The
replays run headless against the null device, whose texture-format list is
Wine's (src/d3d/nullddraw.cpp).  --mode picks Karoo.cfg's display mode index
(3 = 1024x768x32, 10 = 800x600x16); run/Karoo.cfg is put back afterwards.
"""
import argparse, json, os, subprocess, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CFG = os.path.join(REPO, "run", "Karoo.cfg")
MODE_OFFSET = 0x1c


def recordings():
    with open(os.path.join(REPO, "tests", "manifest.json")) as fh:
        return [r["name"] for r in json.load(fh)["recordings"]]


def collect(out, names, mode, fast):
    tmp = out + ".raw"
    if os.path.exists(tmp):
        os.remove(tmp)
    orig = open(CFG, "rb").read()
    try:
        if mode is not None:
            cfg = bytearray(orig)
            cfg[MODE_OFFSET] = mode
            open(CFG, "wb").write(cfg)
        env = dict(os.environ, KAROO_TEXTURE_DUMP=os.path.abspath(tmp))
        for n in names:
            # The pass/fail verdict is not the point here; a different display
            # mode may legitimately move the simulation.
            cmd = [sys.executable, os.path.join(REPO, "tools", "replaytest.py")]
            if not fast:
                cmd.append("--no-fast")
            subprocess.run(cmd + [n], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    finally:
        open(CFG, "wb").write(orig)
    lines = sorted(set(open(tmp).read().splitlines()))
    os.remove(tmp)
    with open(out, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print("%s: %d surfaces" % (out, len(lines)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--diff", nargs=2, metavar=("A", "B"))
    ap.add_argument("--mode", type=int)
    ap.add_argument("--no-fast", dest="fast", action="store_false",
                    help="render for real; --fast skips PresentImage's Blt, so the "
                         "loading screens are never converted")
    ap.add_argument("out", nargs="?")
    ap.add_argument("names", nargs="*")
    a = ap.parse_intermixed_args()
    if a.diff:
        x = open(a.diff[0]).read().splitlines()
        y = open(a.diff[1]).read().splitlines()
        sx, sy = set(x), set(y)
        for l in sorted(sx - sy):
            print("- " + l)
        for l in sorted(sy - sx):
            print("+ " + l)
        print("identical" if sx == sy else "DIFFERENT")
        sys.exit(0 if sx == sy else 1)
    if not a.out:
        ap.error("OUT required")
    collect(a.out, a.names or recordings(), a.mode, a.fast)


main()
