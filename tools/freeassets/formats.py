"""Writers for the file formats the game reads, as far as the game reads them.

Each writer takes plain Python data and returns bytes; nothing here knows what
the content is.  The layouts are the game's own loaders':

    TGA   src/gfx/imagetga.cpp        uncompressed 32-bit, bottom-left origin
    BMP   src/gfx/imagebmp.cpp        uncompressed 24-bit
    WAV   the SDL loader              16-bit mono PCM
    MDL   src/gfx/animatedmesh.cpp    frames of triangle-list vertices
    JJM   src/level/levelmap.cpp      the level map and its trailer
    GAM   src/session/session.cpp     the binary form (tools/karoosave.py reads it)
    FON   src/gfx/textrenderer.cpp    three lines: atlas path, columns, rows
    PAR   src/fx/particles.cpp        a serialised FaceParticleSystem
    LEO   src/level/extraobjects.cpp  text: one ';'-terminated object a line
"""
import math
import struct


# ─── Images ──────────────────────────────────────────────────────────────────
#
# An image is (width, height, pixels): pixels is a list of rows, top row
# first, each row a list of (r, g, b, a) tuples.

def tga(width, height, pixels):
    out = bytearray(struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0,
                                width, height, 32, 8))
    for row in reversed(pixels):  # bottom-left origin
        for r, g, b, a in row:
            out += bytes((b, g, r, a))
    return bytes(out)


def bmp(width, height, pixels):
    stride = (width * 3 + 3) & ~3
    data = bytearray()
    for row in reversed(pixels):  # bottom-up
        line = bytearray()
        for r, g, b, _ in row:
            line += bytes((b, g, r))
        data += line + bytes(stride - len(line))
    header = struct.pack("<2sIHHI", b"BM", 54 + len(data), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(data),
                       2834, 2834, 0, 0)
    return header + info + bytes(data)


# ─── Sound ───────────────────────────────────────────────────────────────────

RATE = 22050


def wav(samples):
    """samples: floats in [-1, 1] at RATE."""
    pcm = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767)) for s in samples)
    fmt = struct.pack("<HHIIHH", 1, 1, RATE, RATE * 2, 2, 16)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + \
        b"data" + struct.pack("<I", len(pcm)) + pcm
    return b"RIFF" + struct.pack("<I", len(body)) + body


def tone(freq, seconds, volume=0.4, slide=0.0, noise=False, seed=1):
    """A plain enveloped tone; slide is the frequency change over the sound,
    noise swaps the sine for a seeded noise burst."""
    n = int(RATE * seconds)
    out = []
    phase = 0.0
    x = seed
    for i in range(n):
        t = i / n
        env = min(1.0, i / (RATE * 0.005)) * (1.0 - t)  # 5 ms attack, linear decay
        if noise:
            x = (x * 1103515245 + 12345) & 0x7fffffff
            s = (x / 0x3fffffff) - 1.0
        else:
            phase += 2 * math.pi * (freq + slide * t) / RATE
            s = math.sin(phase)
        out.append(s * env * volume)
    return out


# ─── Models ──────────────────────────────────────────────────────────────────
#
# A vertex is (pos, normal, uv): three, three and two floats.  The game's
# models are Z-up, about one unit to a cell, and wind each triangle
# counter-clockwise about its normal.

def mdl(frames):
    """frames: a list of vertex lists, all the same length (triangle lists)."""
    count = len(frames[0])
    assert all(len(f) == count for f in frames) and count % 3 == 0
    out = bytearray(struct.pack("<HI", len(frames), count))
    for verts in frames:
        lo = [min(v[0][k] for v in verts) for k in range(3)]
        hi = [max(v[0][k] for v in verts) for k in range(3)]
        out += struct.pack("<6f", *lo, *hi)
        for pos, normal, uv in verts:
            out += struct.pack("<10f", *pos, *normal, *uv, *uv)
    return bytes(out)


def box(x0, y0, z0, x1, y1, z1):
    """The triangles of an axis-aligned box, each face mapped to the whole
    texture."""
    faces = [  # (normal, four corners counter-clockwise seen from outside)
        ((0, 0, 1), [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
        ((0, 0, -1), [(x0, y1, z0), (x1, y1, z0), (x1, y0, z0), (x0, y0, z0)]),
        ((1, 0, 0), [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)]),
        ((-1, 0, 0), [(x0, y1, z0), (x0, y0, z0), (x0, y0, z1), (x0, y1, z1)]),
        ((0, 1, 0), [(x1, y1, z0), (x0, y1, z0), (x0, y1, z1), (x1, y1, z1)]),
        ((0, -1, 0), [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)]),
    ]
    uvs = [(0, 1), (1, 1), (1, 0), (0, 0)]
    tris = []
    for n, c in faces:
        for i in (0, 1, 2, 0, 2, 3):
            tris.append((c[i], n, uvs[i]))
    return tris


def sphere(r, zc, rings=8, segments=12):
    """An orb of radius r centred zc up, smooth-shaded, the texture wrapped
    once around it."""
    def point(i, j):
        th, ph = math.pi * i / rings, 2 * math.pi * j / segments
        n = (math.sin(th) * math.cos(ph), math.sin(th) * math.sin(ph), math.cos(th))
        return ((r * n[0], r * n[1], zc + r * n[2]), n, (j / segments, i / rings))
    tris = []
    for i in range(rings):
        for j in range(segments):
            a, b = point(i, j), point(i + 1, j)
            c, d = point(i + 1, j + 1), point(i, j + 1)
            if i > 0:
                tris += [a, b, d]          # the cap rows are triangles only
            if i < rings - 1:
                tris += [b, c, d]
    return tris


# ─── Particle systems ────────────────────────────────────────────────────────
#
# Particle space is Y-up.  The file is read in text mode (sysdev::TextFile):
# a CR LF pair folds to LF and a 0x1a byte ends the file, so every LF is
# written as CR LF and a 0x1a anywhere is refused.

def _name(s):
    b = s.encode("ascii") + b"\0"
    return struct.pack("<I", len(b)) + b


def par(count, scale, colours, life, speed=0.0, rate=1e6, direction=(0, 1, 0),
        spread=(0, 0, 0), pos=(0, 0, 0), pos_spread=(0, 0, 0), box=None,
        gravity=0.0, fade_ms=200.0, floor=False):
    """A FaceParticleSystem of count particles, each a scale-sized quad.

    colours  [(0xRRGGBB, weight)], picked at random by weight
    life     (mean, variance) in seconds; speed is (mean, variance) or a mean
    rate     particles a second; a huge one bursts the whole count at once
    direction, spread  the launch direction and its per-axis variance
    pos, pos_spread    where they start (a point mode), or box=(lo, hi)
    gravity  pull along -Y; fade_ms how long a particle takes to fade out
    floor    clip them at Y 0 rather than fall through the ground
    """
    speed = speed if isinstance(speed, tuple) else (speed, 0.0)
    out = _name("FaceParticleSystem") + struct.pack("<I", count)
    lo, hi = box if box else ((0, 0, 0), (0, 0, 0))
    out += _name("StdGenerator")
    out += struct.pack("<I3f3f3f3f", 1 if box else 0, *hi, *lo, *pos, *pos_spread)
    out += struct.pack("<3f3f4ff", *direction, *spread, speed[0], speed[1], life[0], life[1], rate)
    out += struct.pack("<I", len(colours)) + b"".join(struct.pack("<II", c, w) for c, w in colours)
    out += _name("GravityEnvironment")
    out += struct.pack("<3ffIfI", 0, -1, 0, gravity, 0, fade_ms, 10)
    out += struct.pack("<3I3f3f", 0, 1 if floor else 0, 0, 0, 100 if floor else 0, 0, 0, 0, 0)
    out += struct.pack("<f", scale)
    if b"\x1a" in out:
        raise ValueError("a 0x1a byte would end the particle file early; nudge a value")
    return out.replace(b"\n", b"\r\n")


def leo(objects):
    """objects: (model, (u, v, height), texture) -- an unlit-blend, lit model
    standing on that cell."""
    lines = ["Model\t%s\t%g,%g,%g\t0,0,0\tNONE\tNONE\tLIT\t%s;" % (m, u, v, h, t)
             for m, (u, v, h), t in objects]
    return ("\r\n".join(lines) + "\r\n").encode("ascii")


# ─── Levels ──────────────────────────────────────────────────────────────────

def jjm(cells, gems, time_limit, theme, title, bonus=0):
    """cells: rows (v) of columns (u) of (height, kind, param, contents).
    The game clears the border cells after reading them, so a map's playable
    area is its inside."""
    height, width = len(cells), len(cells[0])
    out = bytearray((width, height))
    for row in cells:
        for cell in row:
            out += bytes(cell)

    def text(s):
        b = s.encode("ascii")[:0x7f]
        return b + bytes(0x80 - len(b))
    out += struct.pack("<ii", gems, time_limit) + text(theme) + text(title) + text("") + \
        struct.pack("<i", bonus)
    return bytes(out)


def gam(levels):
    """The 6,6,6,<count> header, then a 256-byte name per level, every byte
    plus 5.  The game also reads a text form, but the tools read this one."""
    table = b"".join(n.encode("ascii").ljust(256, b"\0") for n in levels)
    return bytes((6, 6, 6, len(levels))) + bytes((b + 5) & 0xff for b in table)


def fon(atlas, cols, rows):
    return ("%s\r\n%d\r\n%d\r\n" % (atlas, cols, rows)).encode("ascii")
