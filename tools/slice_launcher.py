#!/usr/bin/env python3
"""slice_launcher.py -- cut the launcher's bitmaps from three full mock-ups.

    python3 tools/slice_launcher.py data/launcher OUTDIR

data/launcher holds three 600x400 24-bit BMPs of the whole launcher window,
identical except over the buttons:
    background.bmp   no buttons
    unselected.bmp   every button in its normal state
    selected.bmp     every button in its focused state

Writes to OUTDIR what windev/launcher.cpp draws, compiled into the program:
    launcher_assets.cpp                byte arrays: launcher_bg_bmp (the
                                       background, whole),
                                       launcher_{play,setup,quit}_{off,foc}_bmp
                                       (each button's rectangle from the
                                       unselected / selected mock-up) and
                                       launcher_icon_rgba (the window icon,
                                       from data/openroo.ico)
    launcher_layout.h                  the window size, button rectangles and
                                       the declarations of those arrays

The button rectangles are BUTTONS below, chosen to enclose each label and
its arrows with a margin.  The script refuses to run if the mock-ups differ
anywhere outside them, so a redrawn mock-up whose buttons moved is caught.

The painted title bar and its minimise / close boxes (TITLE_H, MIN_BOX,
CLOSE_BOX) are hit-tested by windev/launcher.cpp: the window has no real
frame.

Standard library only.
"""
import os
import struct
import sys

W, H = 600, 400

# name: (x, y, w, h) in window pixels.
BUTTONS = {
    "play":  (366, 111, 216, 36),
    "setup": (366, 150, 216, 36),
    "quit":  (366, 188, 216, 36),
}
TITLE_H = 27                    # rows 0..26 drag the window
MIN_BOX = (533, 4, 20, 22)      # x, y, w, h
CLOSE_BOX = (574, 4, 22, 22)

# Channel difference (sum of R, G, B) below which pixels count as equal.
TOLERANCE = 60


def read_bmp(path):
    """Rows top to bottom, each a bytes of W*3 BGR."""
    data = open(path, "rb").read()
    if data[:2] != b"BM":
        sys.exit(f"{path}: not a BMP")
    off, = struct.unpack_from("<I", data, 10)
    hsize, w, h, planes, bpp, comp = struct.unpack_from("<IiiHHI", data, 14)
    if (w, abs(h), bpp, comp) != (W, H, 24, 0):
        sys.exit(f"{path}: want {W}x{H} 24-bit uncompressed, got "
                 f"{w}x{h} {bpp}-bit compression {comp}")
    stride = (W * 3 + 3) & ~3
    rows = [data[off + r * stride: off + r * stride + W * 3] for r in range(H)]
    return rows if h < 0 else rows[::-1]


def bmp_bytes(rows):
    h, w = len(rows), len(rows[0]) // 3
    stride = (w * 3 + 3) & ~3
    pad = b"\0" * (stride - w * 3)
    bits = b"".join(r + pad for r in reversed(rows))
    hdr = struct.pack("<2sIHHI", b"BM", 54 + len(bits), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(bits),
                       2835, 2835, 0, 0)
    return hdr + info + bits


def icon_rgba(path, want=48):
    """The 32-bit image of size want in an .ico, as top-down RGBA."""
    d = open(path, "rb").read()
    _, _, n = struct.unpack_from("<HHH", d, 0)
    for i in range(n):
        w, h, _, _, _, bpp, size, off = struct.unpack_from("<BBBBHHII", d, 6 + 16 * i)
        if (w or 256) == want and bpp == 32:
            hdr, = struct.unpack_from("<I", d, off)
            px = d[off + hdr: off + hdr + want * want * 4]   # bottom-up BGRA
            rows = [px[y * want * 4:(y + 1) * want * 4] for y in range(want)][::-1]
            out = bytearray()
            for r in rows:
                for x in range(want):
                    b, g, rr, a = r[x * 4:x * 4 + 4]
                    out += bytes((rr, g, b, a))
            return bytes(out)
    sys.exit(f"{path}: no {want}x{want} 32-bit image")


def c_array(name, data):
    lines = [f"const unsigned char {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 24):
        lines.append(",".join(str(b) for b in data[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)


def crop(rows, x, y, w, h):
    return [r[x * 3:(x + w) * 3] for r in rows[y:y + h]]


def inside_button(x, y):
    return any(bx <= x < bx + bw and by <= y < by + bh
               for bx, by, bw, bh in BUTTONS.values())


def check_outside(bg, other, name):
    for y in range(H):
        a, b = bg[y], other[y]
        if a == b:
            continue
        for x in range(W):
            d = sum(abs(a[x * 3 + i] - b[x * 3 + i]) for i in range(3))
            if d > TOLERANCE and not inside_button(x, y):
                sys.exit(f"{name}: differs from background.bmp at ({x},{y}), "
                         "outside every button rectangle")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, out = sys.argv[1:]
    bg = read_bmp(os.path.join(src, "background.bmp"))
    off = read_bmp(os.path.join(src, "unselected.bmp"))
    foc = read_bmp(os.path.join(src, "selected.bmp"))
    check_outside(bg, off, "unselected.bmp")
    check_outside(bg, foc, "selected.bmp")

    os.makedirs(out, exist_ok=True)
    assets = [("launcher_bg_bmp", bmp_bytes(bg))]
    for name, r in BUTTONS.items():
        assets.append((f"launcher_{name}_off_bmp", bmp_bytes(crop(off, *r))))
        assets.append((f"launcher_{name}_foc_bmp", bmp_bytes(crop(foc, *r))))
    icon = icon_rgba(os.path.join(src, "..", "openroo.ico"))

    cpp = ["/* Generated by tools/slice_launcher.py -- do not edit. */",
           '#include "launcher_layout.h"', ""]
    for name, data in assets + [("launcher_icon_rgba", icon)]:
        cpp.append(c_array(name, data))
        cpp.append("")
    with open(os.path.join(out, "launcher_assets.cpp"), "w") as fh:
        fh.write("\n".join(cpp))

    lines = ["/* Generated by tools/slice_launcher.py -- do not edit. */",
             "#pragma once",
             f"#define LAUNCHER_W {W}",
             f"#define LAUNCHER_H {H}",
             f"#define LAUNCHER_TITLE_H {TITLE_H}",
             "#define LAUNCHER_ICON_SIZE 48"]
    for name, r in list(BUTTONS.items()) + [("min", MIN_BOX), ("close", CLOSE_BOX)]:
        lines.append(f"#define LAUNCHER_{name.upper()}_RECT " + ", ".join(map(str, r)))
    lines.append('extern "C++" {')
    for name, data in assets + [("launcher_icon_rgba", icon)]:
        lines.append(f"extern const unsigned char {name}[{len(data)}];")
    lines.append("}")
    with open(os.path.join(out, "launcher_layout.h"), "w") as fh:
        fh.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
