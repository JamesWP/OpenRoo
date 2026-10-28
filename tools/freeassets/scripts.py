"""The Basic levels' instruction scripts (.jjs) and their voices.

A level's script, InstructionScripts/<theme>/<level>.jjs, plays before the
level starts (script/scriptplayer.cpp): here a sweep of the camera round the
map, then a look at the start while its captions show, each with a voice.
Enter skips it.

The script language, as the game reads it: entries end with ';' and a
newline (so a caption holds no ';', and a newline inside one breaks the
line on screen).

    initwave <wav> <id>        prepare a streamed sound under id
    playwave <id> [WAIT]       start it; WAIT holds the script until it ends
    observe 1 | 2              1: the camera rides the spline; 2: it looks at
                               the gotoxyz point from distance away
    setcamposxyz / setcamtargetxyz u,v,h
                               the spline's start and what it looks at
    splinexyz <seconds> u,v,h ...   the camera's path through those points
    distance <d>   gotoxyz u,v,h   movetoxyz u,v,h,<speed>
    text <caption>   wait <seconds>   break (the end: play begins)

Coordinates are a cell's (u, v) and a height, as the map's cells are.

There is no recorded voice: each caption plays a babble, a run of short
pitched syllables about as long as the caption takes to read, seeded by
its text so it is the same every run.
"""
import math
import random
import zlib

from . import formats as F
from . import levels

# Each level's captions, in order.  Keep them short: about 48 characters to a
# line.
INTROS = {
    "First":  ["Welcome to Open Roo.",
               "Collect every crystal, then reach\nthe green exit before time runs out."],
    "Gaps":   ["Holes everywhere.", "Step off the edge and you fall:\nwatch where you walk."],
    "Climb":  ["Stairs take you up one step at a time.",
               "The lift at the end carries you\nto the top."],
    "Foes":   ["You are not alone here.", "The catcher hunts you down.\nKeep moving."],
    "Pads":   ["Teleporters carry you across the gaps.",
               "Each pad sends you to its partner."],
    "Switch": ["No way across... yet.", "Stand on the switch to raise the bridge."],
    "Floors": ["Mind the floor.", "Ice slides, glue holds you,\nand yellow tiles fall away."],
    "Air":    ["Ride the platform across.", "Then take the paraglider\nand step off the edge."],
    "Bonus":  ["Help yourself.",
               "Freeze, protection, speed, time and lives.\nThe boxes are a surprise."],
    "Bombs":  ["Pick up the bombs.", "Blow the blocks apart to reach the exit."],
}

SPEED = 14.0     # characters a second a caption is read at
SYLLABLE = 0.11  # seconds


def babble(text):
    """A voice for text: syllables at random pitches, falling at the end."""
    rnd = random.Random(zlib.crc32(text.encode()))
    count = max(3, int(len(text) / SPEED / SYLLABLE))
    out = []
    base = rnd.uniform(240, 320)
    for i in range(count):
        f = base * rnd.choice((1.0, 1.125, 1.25, 1.5)) * (0.8 if i == count - 1 else 1.0)
        n = int(F.RATE * SYLLABLE * rnd.uniform(0.6, 1.0))
        for k in range(n):
            t = k / n
            env = math.sin(math.pi * t) ** 0.5
            out.append(0.3 * env * (math.sin(2 * math.pi * f * k / F.RATE)
                                    + 0.3 * math.sin(4 * math.pi * f * k / F.RATE)))
        out += [0.0] * int(F.RATE * SYLLABLE * rnd.uniform(0.1, 0.4))
    return F.wav(out)


def script(theme, name, cells, captions):
    """The script for one level: a sweep round the map, its captions, then
    the camera down onto the start."""
    rows, cols = len(cells), len(cells[0])
    tops = [c[0] for row in cells for c in row if c[0]]
    lo, hi = min(tops), max(tops)
    start = next((u, v, c[0]) for v, row in enumerate(cells) for u, c in enumerate(row) if c[1] == 0x03)
    cu, cv, ch = cols / 2, rows / 2, (lo + hi) / 2
    up = hi + 6

    def at(u, v, h):
        return "%g,%g,%g" % (u, v, h)

    lines = []
    for i, _ in enumerate(captions):
        lines.append("initwave waves\\%s\\Voice\\%s_%02d.wav %d" % (theme, name, i + 1, i))
    # Round the map's corners, high up, looking at its middle.
    path = [(-4, -4, up + 4), (cu, -5, up + 2), (cols + 4, -4, up + 2), (cols + 5, cv, up),
            (cols + 4, rows + 4, up), (cu, rows + 5, up + 2), (-4, rows + 4, up + 4),
            (-4, rows + 4, up + 4)]
    lines += ["observe 1", "text\n", "setcamposxyz " + at(*path[0]),
              "setcamtargetxyz " + at(cu, cv, ch),
              "splinexyz 10\n" + "\n".join(at(*p) for p in path), "wait 9"]
    # Then over the start, the captions over it.
    lines += ["observe 2", "distance 8", "gotoxyz " + at(cu, cv, ch),
              "movetoxyz %s,4" % at(*start)]
    for i, caption in enumerate(captions):
        lines += ["text\n" + caption, "playwave %d" % i,
                  "wait %g" % max(2.5, round(len(caption) / SPEED + 1, 1))]
    lines += ["distance 3", "text\n", "wait 1", "break"]
    return ("".join(l + ";\r\n" for l in lines)).replace("\r\n", "\n").replace("\n", "\r\n").encode("ascii")


def files(theme):
    out = {}
    for name, _title, _limit, text in levels.LEVELS:
        captions = INTROS.get(name)
        if not captions:
            continue
        cells, _ = levels.compile_map(text)
        out["InstructionScripts/%s/%s.jjs" % (theme, name)] = script(theme, name, cells, captions)
        for i, caption in enumerate(captions):
            out["waves/%s/Voice/%s_%02d.wav" % (theme, name, i + 1)] = babble(caption)
    return out
