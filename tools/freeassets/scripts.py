"""The Basic levels' instruction scripts (.jjs) and their voices.

A level's script, InstructionScripts/<theme>/<level>.jjs, plays before the
level starts (script/scriptplayer.cpp): here a sweep of the camera round the
map, then the camera visits the objects its captions describe, each caption
with a voice, and settles on the start.
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

# Each level's captions, in order, each with the map glyph the camera moves
# over while it shows (the first such cell; None stays where it is, "S" is
# the start).  Keep them short: about 48 characters to a line.
INTROS = {
    "First":  [("Welcome to Open Roo.", "S"),
               ("Collect every crystal...", "*"),
               ("...then reach the green exit\nbefore time runs out.", "E")],
    "Gaps":   [("Holes everywhere.", None),
               ("Step off the edge and you fall:\nwatch where you walk.", "S")],
    "Climb":  [("Stairs take you up one step at a time.", ">"),
               ("The lift at the end carries you\nto the top.", "=")],
    "Foes":   [("You are not alone here.", None),
               ("The catcher hunts you down.\nKeep moving.", "C"),
               ("Blocks stand in everyone's way.", "#")],
    "Pads":   [("Teleporters carry you across the gaps.", "a"),
               ("Each pad sends you to its partner.", "b")],
    "Switch": [("No way across... yet.", "-"),
               ("Stand on the switch to raise the bridge.", "s")],
    "Floors": [("Ice slides you along.", "i"),
               ("Glue holds you, but only once.", "g"),
               ("Yellow tiles fall away\nafter you step off.", "d"),
               ("The snowflake freezes your foes.", "F")],
    "Air":    [("Ride the platform across.", "P"),
               ("Beware the thrower.", "T"),
               ("The lift takes you up.", "="),
               ("Take the paraglider\nand step off the edge.", "p")],
    "Bonus":  [("Help yourself.", "S"),
               ("Bombs, lives, freeze and time.", "L"),
               ("Protection keeps the catcher off.", "o"),
               ("Speed... and surprises.", "?"),
               ("Here comes the catcher.", "C")],
    "Bombs":  [("Pick up the bombs.", "B"),
               ("Blow the blocks apart to reach the exit.", "x")],
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


def find(text, glyph):
    """The (u, v) of the first glyph in a map's text."""
    for v, row in enumerate(text.strip("\n").split("\n")):
        if glyph in row:
            return row.index(glyph), v
    raise ValueError("no %r on the map" % glyph)


def script(theme, name, text, captions):
    """The script for one level: a sweep round the map, then a visit to each
    caption's object while it shows, ending on the start."""
    cells, _ = levels.compile_map(text)
    rows, cols = len(cells), len(cells[0])
    tops = [c[0] for row in cells for c in row if c[0]]
    lo, hi = min(tops), max(tops)
    cu, cv, ch = cols / 2, rows / 2, (lo + hi) / 2
    up = hi + 6

    def at(u, v, h):
        return "%g,%g,%g" % (u, v, h)

    def cell(glyph):
        u, v = find(text, glyph)
        return u, v, cells[v][u][0] or ch

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
    # Then close in on each object in turn, its caption showing.
    lines += ["observe 2", "distance 8", "gotoxyz " + at(cu, cv, ch), "distance 5"]
    for i, (caption, glyph) in enumerate(captions):
        if glyph:
            lines += ["movetoxyz %s,6" % at(*cell(glyph))]
        lines += ["text\n" + caption, "playwave %d" % i,
                  "wait %g" % max(2.5, round(len(caption) / SPEED + 1, 1))]
    lines += ["text\n", "movetoxyz %s,6" % at(*cell("S")), "distance 3", "wait 1", "break"]
    return ("".join(l + ";\n" for l in lines)).replace("\n", "\r\n").encode("ascii")


def files(theme):
    out = {}
    for name, _title, _limit, text in levels.LEVELS:
        captions = INTROS.get(name)
        if not captions:
            continue
        out["InstructionScripts/%s/%s.jjs" % (theme, name)] = script(theme, name, text, captions)
        for i, (caption, _glyph) in enumerate(captions):
            out["waves/%s/Voice/%s_%02d.wav" % (theme, name, i + 1)] = babble(caption)
    return out
