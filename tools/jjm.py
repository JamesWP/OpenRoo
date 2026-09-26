#!/usr/bin/env python3
"""Parse a Ka'roo level file (Levels/<Theme>/<Name>.jjm).

Layout, confirmed against all 88 shipped level files:

    +0x00  1  byte   width   (cells)
    +0x01  1  byte   height  (cells)
    +0x02  2  bytes  unknown, zero in every shipped level
    +0x04     width*height * 4-byte cells, row-major (y outer, x inner)
    ...       394-byte trailer

The size relation `len(file) == 398 + width*height*4` holds exactly for all 88
files, which is what fixes the cell size at 4 and the non-cell bytes at 398.

Cell (4 bytes):

    [0]  unknown  (zero in ~99% of cells; a few small values)
    [1]  unknown  (mostly 0/1)
    [2]  height   0 = air/no tile, otherwise the tile's top height
    [3]  type     terrain/object kind (0 = plain, others index the theme)

**Height is byte[2], not byte[3].** That was established by dumping the
vertices the engine actually builds for the tile-side skirts
(KAROO_QUAD_DUMP=<path>, see src/render/quadbatch.cpp) and comparing the set
of quad top-Y values against the file: for Castle\\Something the engine emits
skirt tops at y in {5,6,7,8,9,10,19,20}, which is exactly the set of distinct
byte[2] values, including the distinctive 19/20 pair.  byte[3]'s value set does
not match.

The trailer holds the theme name as a NUL-terminated string at +0x06 (e.g.
"Castle") followed by editor leftovers -- the shipped files still contain the
level editor's German file-dialog string "Alle Dateien (*.*)".

World mapping (from the dumped vertices): cell (cx, cy) is centred at world
(x=cx, z=-cy) and spans +-0.5 in both, so cell boundaries fall on half
integers.  A tile of height H has its top face at y=H.

Usage:
  jjm.py info   <file.jjm>            # header, cell stats, theme
  jjm.py map    <file.jjm> [--field height|type]
  jjm.py cells  <file.jjm>            # raw cell dump
"""
import sys, os, collections

HEADER = 4
TRAILER = 394


def load(path):
    d = open(path, 'rb').read()
    w, h = d[0], d[1]
    if len(d) != HEADER + w * h * 4 + TRAILER:
        raise SystemExit(f"{path}: size {len(d)} != {HEADER + w*h*4 + TRAILER} "
                         f"expected for {w}x{h} — not a .jjm, or a variant layout")
    cells = [[d[HEADER + (y * w + x) * 4: HEADER + (y * w + x) * 4 + 4]
              for x in range(w)] for y in range(h)]
    trailer = d[HEADER + w * h * 4:]
    theme = trailer[6:trailer.index(b'\0', 6)].decode('latin-1') if b'\0' in trailer[6:] else ''
    return w, h, cells, trailer, theme


def cmd_info(path):
    w, h, cells, trailer, theme = load(path)
    print(f"{path}: {w} x {h} = {w*h} cells, theme {theme!r}")
    for i, name in ((0, 'byte0   '), (1, 'byte1   '), (2, 'height  '), (3, 'type    ')):
        c = collections.Counter(cells[y][x][i] for y in range(h) for x in range(w))
        print(f"  {name}: {dict(sorted(c.items()))}")
    solid = sum(1 for y in range(h) for x in range(w) if cells[y][x][2])
    print(f"  solid (height>0): {solid}   air: {w*h - solid}")


def cmd_map(path, field='height'):
    w, h, cells, trailer, theme = load(path)
    idx = {'height': 2, 'type': 3}[field]
    print(f"{path}  {w}x{h}  theme={theme}  field={field}")
    print("    " + "".join(f"{x%10}" for x in range(w)))
    for y in range(h):
        row = ""
        for x in range(w):
            v = cells[y][x][idx]
            row += "." if v == 0 else (f"{v:x}" if v < 16 else "#")
        print(f"{y:3} {row}")


def cmd_cells(path):
    w, h, cells, trailer, theme = load(path)
    for y in range(h):
        for x in range(w):
            c = cells[y][x]
            if c[2] or c[3] or c[0] or c[1]:
                print(f"({x:3},{y:3}) b0={c[0]:3} b1={c[1]:3} height={c[2]:3} type={c[3]:3}")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__.strip())
    cmd, path = sys.argv[1], sys.argv[2]
    if cmd == 'info':
        cmd_info(path)
    elif cmd == 'map':
        field = 'height'
        if '--field' in sys.argv:
            field = sys.argv[sys.argv.index('--field') + 1]
        cmd_map(path, field)
    elif cmd == 'cells':
        cmd_cells(path)
    else:
        sys.exit(__doc__.strip())


if __name__ == '__main__':
    main()
