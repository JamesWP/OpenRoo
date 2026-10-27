"""The Basic levels, drawn as text and compiled to .jjm.

Each map is rows of one character per cell.  The game clears the outermost
ring of cells after reading it, so every map keeps a border of '.'.

    .  nothing (a hole)          1  floor           2  raised floor
    S  the player's start        E  the exit        *  a crystal on floor
    C  a catcher on floor        T  a thrower on floor
    a b  teleporter pads, paired by letter: arriving at one carries the
         player on one step past the other, so leave floor beyond each
    j  jump pad   g  glue   i  ice   d  falling floor   x  bombable block
    #  impassable block   B  bombs   L  extra life   F  freeze   +  time

Kinds and contents are level/tile.h's.
"""
from . import formats as F

LOW, HIGH = 5, 10

# glyph: (height, kind, param, contents)
CELLS = {
    ".": (0, 0x00, 0, 0x00),
    "1": (LOW, 0x01, 0, 0x00),
    "2": (HIGH, 0x01, 0, 0x00),
    "S": (LOW, 0x03, 3, 0x00),      # param: the facing, 1..4 (tile.h DIR_*)
    "E": (LOW, 0x04, 0, 0x00),
    "*": (LOW, 0x01, 0, 0x01),
    "C": (LOW, 0x01, 0, 0x02),
    "T": (LOW, 0x01, 0, 0x03),
    "a": (LOW, 0x0f, 1, 0x00),
    "b": (LOW, 0x0f, 2, 0x00),
    "j": (LOW, 0x0e, 15, 0x00),     # param: the height the jump peaks at
    "g": (LOW, 0x02, 0, 0x00),
    "i": (LOW, 0x15, 0, 0x00),
    "d": (LOW, 0x0d, 0, 0x00),
    "x": (LOW, 0x17, 0, 0x00),
    "#": (LOW, 0x16, 0, 0x00),
    "B": (LOW, 0x01, 0, 0x09),
    "L": (LOW, 0x01, 0, 0x07),
    "F": (LOW, 0x01, 0, 0x08),
    "+": (LOW, 0x01, 0, 0x06),
}

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
    ("Floors", "Slippery", 180, """
............
.S11iiii1*1.
.1111111111.
.1gg1111dd1.
.1*g1111d*1.
.1111111111.
.F1iiiii11E.
............
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
    rows = [r for r in text.strip("\n").split("\n")]
    width = max(len(r) for r in rows)
    cells = [[CELLS[c] for c in r.ljust(width, ".")] for r in rows]
    return cells, sum(c[3] == 0x01 for row in cells for c in row)


def files(theme):
    out = {}
    names = []
    for name, title, limit, text in LEVELS:
        cells, gems = compile_map(text)
        out["Levels/%s/%s.jjm" % (theme, name)] = F.jjm(cells, gems, limit, theme, title)
        names.append("%s\\%s" % (theme, name))
    for name, text in BACKDROPS.items():
        cells, _ = compile_map(text)
        out["Levels/%s.jjm" % name] = F.jjm(cells, 0, 120, theme, "")
    # After the last level: Final\<game file>.
    cells, _ = compile_map(BACKDROPS["DemoLevelForest"])
    out["Levels/Final/JJ.jjm"] = F.jjm(cells, 0, 120, theme, "")
    out["JJ.GAM"] = F.gam(names)
    return out
