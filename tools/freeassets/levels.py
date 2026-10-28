"""The Basic levels, drawn as text and compiled to .jjm.

Each map is rows of one character per cell.  The game clears the outermost
ring of cells after reading it, so every map keeps a border of '.'.

    .  nothing (a hole)
    1..9  floor at height 5..13: a step up of one needs stairs, two or more
          up is a wall the player falls under, three or more down a fatal drop
    S  the player's start        E  the exit        *  a crystal
    C  a catcher                 T  a thrower
    a b  teleporter pads, paired by letter: arriving at one carries the
         player on one step past the other, so leave floor beyond each
    j  jump pad   g  glue   i  ice   d  falling floor   x  bombable block
    #  impassable block   B  bombs   L  extra life   F  freeze   +  time
    ^ > v <  stairs, climbing one height toward the side the arrow points
    =  a lift, between the lowest and highest floor beside it
    P  a platform, riding along +u over the holes ahead to the ground beyond
    p  a paraglider: step off an edge with it and glide down over holes
    - |  a bridge along u or v, over the holes on its open side to the
         ground beyond, worked by the switch s

Everything that is not floor or a hole stands at the height of the floor
around it (the commonest digit among its four neighbours; 5 if none).  The
one switch works every bridge on its map.

Kinds and contents are level/tile.h's.  An impassable cell is drawn as
nothing by the map; what stands on it is the level's .leo object there, so
each '#' gets a block in Level3DExtraObjects.
"""
from collections import Counter

from . import formats as F

LOW = 5

# glyph: (kind, param, contents), at the height of the floor around it.
CELLS = {
    "S": (0x03, 3, 0x00),       # param: the facing, 1..4 (tile.h DIR_*)
    "E": (0x04, 0, 0x00),
    "*": (0x01, 0, 0x01),
    "C": (0x01, 0, 0x02),
    "T": (0x01, 0, 0x03),
    "a": (0x0f, 1, 0x00),
    "b": (0x0f, 2, 0x00),
    "j": (0x0e, 15, 0x00),      # param: the height the jump peaks at
    "g": (0x02, 0, 0x00),
    "i": (0x15, 0, 0x00),
    "d": (0x0d, 0, 0x00),
    "x": (0x17, 0, 0x00),
    "#": (0x16, 0, 0x00),
    "B": (0x01, 0, 0x09),
    "L": (0x01, 0, 0x07),
    "F": (0x01, 0, 0x08),
    "+": (0x01, 0, 0x06),
    "p": (0x01, 0, 0x05),       # a paraglider: one glide, opened 3 below an edge
    "P": (0x0a, 0, 0x00),       # a platform shuttling +u over the holes ahead
    "s": (0x11, 1, 0x00),       # param: the switch's number
    "-": (0x12, 1, 0x00),       # param: the switch that works it
    "|": (0x13, 1, 0x00),
}

# A stair's kind and the way (du, dv) to its high side: kind - 4 is its
# direction, but the high side is ahead for 6 and 8 and behind for 5 and 7.
STAIRS = {">": (0x06, (1, 0)), "<": (0x08, (-1, 0)), "v": (0x05, (0, 1)), "^": (0x07, (0, -1))}

# (name, title, time limit in seconds, map)
LEVELS = [
    ("First", "First steps", 120, """
..........
.S111111..
.1111*11..
.11*1111..
.1111111..
.1*111E1..
..........
"""),
    ("Gaps", "Mind the gap", 150, """
............
.S11.111*11.
.111.11111..
.11111.1*1..
.....1.111..
.*11111..11.
.111..1111E.
............
"""),
    ("Climb", "Up and up", 150, """
................
.S11>22>33=5555.
.1*1.2*2.3*.5*5.
.111.222.33.555.
............5E5.
................
"""),
    ("Foes", "Company", 180, """
............
.S111111111.
.11*1111*11.
.111#11#111.
.1111111111.
.111#11#111.
.11*1111*11.
.111111111C.
.E111111111.
............
"""),
    ("Pads", "Elsewhere", 180, """
.............
.S11a...b11*.
.1111...1111.
.1*11...11*1.
.1111...1111.
.............
.a11b........
.1*1E........
.............
"""),
    ("Switch", "Bridge the gap", 150, """
...............
.S111s1........
.1*1111-...1>2.
.111111....1*2.
.1*1111....12E.
...............
"""),
    ("Floors", "Slippery", 180, """
............
.S11iiii1*1.
.1111111111.
.1gg111ddd1.
.1*g111d*d1.
.111111ddd1.
.F1iiiii11E.
............
"""),
    ("Air", "Ride and glide", 240, """
.....................
.S11P....1111111.....
.1*1.....1*11111=999.
.111.....11111111999.
.........1111111.9*9.
.........11111T1.9p9.
.................999.
.....................
.................11E.
.................1*1.
.....................
"""),
    ("Bombs", "Clear the way", 180, """
...........
.S1111111..
.11B111*1..
.111111x1..
.*1111xxx..
.11111xEx..
...........
"""),
]

# The menu's backdrop and the last level's backdrop: the code opens these by
# name (core/gameboot.cpp, sequencer/playflow.cpp).
BACKDROPS = {
    "DemoLevelForest": """
..........
.11111111.
.11*11*11.
.1111S111.
.11*11*11.
.111E1111.
..........
""",
}


def compile_map(text):
    """The map's cells, rows (v) of (height, kind, param, contents), and its
    crystal count."""
    rows = text.strip("\n").split("\n")
    width = max(len(r) for r in rows)
    rows = [r.ljust(width, ".") for r in rows]

    def floor(u, v):
        c = rows[v][u] if 0 <= v < len(rows) and 0 <= u < width else "."
        return LOW + int(c) - 1 if c.isdigit() else None

    def around(u, v):
        return [h for h in (floor(u + 1, v), floor(u - 1, v), floor(u, v + 1), floor(u, v - 1))
                if h is not None]

    cells = []
    for v, row in enumerate(rows):
        out = []
        for u, c in enumerate(row):
            if c == ".":
                out.append((0, 0, 0, 0))
            elif c.isdigit():
                out.append((floor(u, v), 0x01, 0, 0))
            elif c in STAIRS:
                kind, (du, dv) = STAIRS[c]
                low, high = floor(u - du, v - dv), floor(u + du, v + dv)
                assert low is not None and high == low + 1, "stair at (%d,%d) must climb one" % (u, v)
                out.append((low, kind, 0, 0))
            elif c == "=":
                hs = around(u, v)
                assert len(set(hs)) > 1, "lift at (%d,%d) needs floor at two heights" % (u, v)
                out.append((min(hs), 0x09, max(hs), 0))   # param: the top
            else:
                hs = around(u, v)
                h = Counter(hs).most_common(1)[0][0] if hs else LOW
                out.append((h,) + CELLS[c])
        cells.append(out)
    return cells, sum(c[3] == 0x01 for row in cells for c in row)


def files(theme):
    out = {}
    names = []
    for name, title, limit, text in LEVELS:
        cells, gems = compile_map(text)
        out["Levels/%s/%s.jjm" % (theme, name)] = F.jjm(cells, gems, limit, theme, title)
        blocks = [("models\\basic\\block.mdl", (u, v, c[0]), "textures\\basic\\block.tga")
                  for v, row in enumerate(cells) for u, c in enumerate(row) if c[1] == 0x16]
        if blocks:
            out["Level3DExtraObjects/%s/%s.leo" % (theme, name)] = F.leo(blocks)
        names.append("%s\\%s" % (theme, name))
    for name, text in BACKDROPS.items():
        cells, _ = compile_map(text)
        out["Levels/%s.jjm" % name] = F.jjm(cells, 0, 120, theme, "")
    # After the last level: Final\<game file>.
    cells, _ = compile_map(BACKDROPS["DemoLevelForest"])
    out["Levels/Final/JJ.jjm"] = F.jjm(cells, 0, 120, theme, "")
    out["JJ.GAM"] = F.gam(names)
    return out
