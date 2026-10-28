"""The interface art: the HUD, the radar and needle, the menu panel, its
headings and widgets.

Each image is drawn against where the game puts it, in the 640x480 space the
layout code uses (screens/hud.cpp, screens/menuscreens.cpp):

    HUD      256x256; the top half is the left corner panel, drawn 1:1 at
             (0..256, 0..126), the bottom half the right one at (384..640,
             0..126).  The timer sits between them, about x 208..432 and
             y 0..42, so neither panel reaches in there.
    needle   a 128x128 quad turned about its centre at (541, 92): straight
             up at half vitality, left at none, right at full.
    radar    a 128x128 disc at (576, 416); the foes are white blips on it.
    menu     256x256 at (192..448, 112..368); the controls page and the score
             overlay stretch its middle fifth over the screen.
    menu_N   two 128x64 headings each, drawn at (256..384, 112..176).
    widgets  20x20: the toggle's on and off, a dial and its knob, which
             turns through 270 degrees, and the cursor marker, mirrored on
             the right.
"""
import math

from .canvas import Canvas, arc, circle, polygon, rect, ring, segment

PANEL, PANEL_A = 0x14284a, 0.8
EDGE = 0x8fb4ff
TEXT = 0xffffff

# The headings, in each menu_N texture's order: top half, bottom half.
HEADINGS = {
    1: ("MAIN MENU", "LOAD GAME"),
    2: ("HIGHSCORES", "OPTIONS"),
    3: ("CONTROLS", "VIDEO"),
    4: ("SOUND", "SAVE GAME"),
}


def framed(c, shape, box=None):
    """A rim and a translucent inside: drawn as two shapes so the inside's
    alpha is not stacked on the rim's."""
    inner = lambda x, y: shape(x, y) + 3
    rim = lambda x, y: max(shape(x, y), -inner(x, y))
    c.fill(rim, EDGE, box=box)
    c.fill(inner, PANEL, PANEL_A, box=box)


def hud():
    c = Canvas(256, 256)
    # Left panel, screen (0..256, 0..126) = texture rows 0..126.  Kept left of
    # x 190, clear of the timer; its far corner is rounded off.
    framed(c, rect(-40, -40, 186, 92, 34), box=(0, 0, 256, 127))
    c.fill(circle(76, 40, 7), 0x40ffff)                      # crystals: "n/m" at (10, 32)
    c.fill(circle(74, 38, 2.5), 0xffffff, 0.7)
    c.fill(polygon([(98, 52), (118, 40), (110, 54)]), 0x40e040)  # gliders: "x0" at (121, 38)
    c.fill(circle(66, 70, 7), 0x202020)                      # bombs: "nx" centred on (32, 62)
    c.fill(segment(70, 64, 75, 58, 2), 0xffc040)

    # Right panel, screen (384..640, 0..126) = texture rows 130..256, x - 384.
    oy, ox = 130, -384
    framed(c, rect(448 + ox, -40 + oy, 680 + ox, 126 + oy, 40), box=(0, 130, 256, 256))
    px, py = 541 + ox, 92 + oy                               # the needle's pivot
    span = 0.9519978
    steps = 24
    for i in range(steps):                                   # vitality: red, left, to green
        t = i / (steps - 1)
        col = (int(255 * (1 - t)) << 16) | (int(220 * t) << 8) | 0x30
        a0 = -span + 2 * span * i / steps
        c.fill(arc(px, py, 50, 60, a0 - 0.01, a0 + 2 * span / steps), col, box=(0, 130, 256, 256))
    c.fill(circle(px, py, 7), EDGE, box=(0, 130, 256, 256))
    c.fill(rect(470 + ox - 6, 8 + oy, 470 + ox + 6, 22 + oy, 2), 0x3070e0)  # lives: "xN" at (490, 6)
    c.fill(ring(616 + ox, 31 + oy, 12, 14), EDGE)            # the level number, right at 630
    return c.tga()


def needle():
    c = Canvas(128, 128)
    c.fill(polygon([(64 - 3, 64), (64, 10), (64 + 3, 64)]), 0xff3030)
    return c.tga()


def radar():
    c = Canvas(128, 128)
    framed(c, circle(64, 64, 63))
    for r in (21, 42):
        c.fill(ring(64, 64, r - 0.6, r + 0.6), EDGE, 0.5)
    c.fill(segment(64, 6, 64, 122, 1.2), EDGE, 0.5)
    c.fill(segment(6, 64, 122, 64, 1.2), EDGE, 0.5)
    c.fill(circle(64, 64, 3), 0x3070e0)
    return c.tga()


def menu():
    c = Canvas(256, 256)
    framed(c, rect(2, 2, 254, 254, 26))
    return c.tga()


def headings(top, bottom):
    c = Canvas(128, 128)
    for word, y0 in ((top, 0), (bottom, 64)):
        framed(c, rect(4, y0 + 8, 124, y0 + 56, 16), box=(0, y0, 128, y0 + 64))
        scale = 2 if len(word) * 12 <= 112 else 1.5
        c.text(word, 64, y0 + 32, scale, TEXT, outline=0x000000)
    return c.tga()


def toggle(on):
    c = Canvas(32, 32)
    c.fill(circle(16, 16, 14), EDGE)
    c.fill(circle(16, 16, 11), 0x30e040 if on else 0x303840)
    if on:
        c.fill(circle(13, 12, 4), 0xffffff, 0.5)
    return c.tga()


def dial():
    """The knob's scale: ticks round 270 degrees, the gap at the bottom."""
    c = Canvas(32, 32)
    c.fill(circle(16, 16, 15), PANEL, PANEL_A)
    for i in range(11):
        a = -0.75 * math.pi + 1.5 * math.pi * i / 10
        s, k = math.sin(a), -math.cos(a)
        c.fill(segment(16 + 11 * s, 16 + 11 * k, 16 + 14 * s, 16 + 14 * k, 1.6), EDGE)
    return c.tga()


def knob():
    """Turned by the game: its mark points up at mid value."""
    c = Canvas(32, 32)
    c.fill(circle(16, 16, 9), 0xc0c8d8)
    c.fill(segment(16, 15, 16, 4, 3), 0xff4040)
    return c.tga()


def selector():
    """The cursor marker, pointing right at the row; mirrored on the right."""
    c = Canvas(32, 32)
    c.fill(polygon([(6, 6), (28, 16), (6, 26), (12, 16)]), 0xffe040)
    return c.tga()


def logo():
    c = Canvas(128, 128)
    framed(c, rect(4, 24, 124, 104, 20))
    c.text("OPEN", 64, 48, 3, TEXT, outline=0x000000)
    c.text("ROO", 64, 80, 3, 0x40ffff, outline=0x000000)
    return c.tga()


def textures():
    t = {
        "textures/karoo128.tga": logo(),
        "textures/selector.tga": selector(),
        "textures/knopf_ein.tga": toggle(True),
        "textures/knopf_aus.tga": toggle(False),
        "textures/drehknopf.tga": knob(),
        "textures/scale.tga": dial(),
        "textures/basic/hud.tga": hud(),
        "textures/basic/menu.tga": menu(),
        "textures/basic/pointer.tga": needle(),
        "textures/basic/radar.tga": radar(),
    }
    for n, (top, bottom) in HEADINGS.items():
        t["textures/menu_%d.tga" % n] = headings(top, bottom)
    return t
