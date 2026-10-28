"""The placeholder content: every file the game needs, made from nothing.

files() returns {path under game/: bytes}.  It is deterministic, so two runs
make identical files.  The look is deliberately plain -- flat colours, boxes,
beeps and sparks -- and every name is one the game opens itself or one Basic.thm or
the levels name; nothing is copied or derived from the original game's data.
"""
import math

from . import formats as F
from . import levels
from . import ui
from .font5x7 import glyph

THEME = "Basic"


# ─── Pixel helpers ───────────────────────────────────────────────────────────

def rgb(hexcolour, a=255):
    return ((hexcolour >> 16) & 0xff, (hexcolour >> 8) & 0xff, hexcolour & 0xff, a)


def image(size, fn):
    """A size x size image whose pixel (x, y) is fn(x / size, y / size)."""
    return F.tga(size, size, [[fn((x + 0.5) / size, (y + 0.5) / size) for x in range(size)]
                              for y in range(size)])


def solid(colour, size=8, a=255):
    c = rgb(colour, a)
    return image(size, lambda u, v: c)


def tile(colour, edge, size=64, mark=None):
    """A floor tile: a flat colour with a darker border, and an optional
    mark(u, v) -> bool drawn in the edge colour."""
    c, e = rgb(colour), rgb(edge)

    def px(u, v):
        if min(u, v, 1 - u, 1 - v) < 0.06:
            return e
        if mark and mark(u, v):
            return e
        return c
    return image(size, px)


def disc(colour, size=32, soft=False):
    """A filled circle on transparency."""
    r, g, b, _ = rgb(colour)

    def px(u, v):
        d = math.hypot(u - 0.5, v - 0.5) * 2
        if soft:
            return (r, g, b, int(255 * max(0.0, 1 - d)))
        return (r, g, b, 255 if d < 0.9 else 0)
    return image(size, px)


def ring(colour, size=32):
    r, g, b, _ = rgb(colour)

    def px(u, v):
        d = math.hypot(u - 0.5, v - 0.5) * 2
        return (r, g, b, 255 if 0.7 < d < 0.95 else 0)
    return image(size, px)


def gradient(top, bottom, size=64):
    t, b = rgb(top), rgb(bottom)
    return image(size, lambda u, v: tuple(int(t[k] + (b[k] - t[k]) * v) for k in range(4)))


def font_atlas(size, cols, rows, chars, scale):
    """White glyphs on transparency, cell i holding chars[i]."""
    cw, ch = size // cols, size // rows
    pix = [[(255, 255, 255, 0)] * size for _ in range(size)]
    for i, c in enumerate(chars):
        g = glyph(c) if c else None
        if g is None:
            continue
        ox = (i % cols) * cw + (cw - 5 * scale) // 2
        oy = (i // cols) * ch + (ch - 7 * scale) // 2
        for gy, bits in enumerate(g):
            for gx in range(5):
                if bits & (0x10 >> gx):
                    for sy in range(scale):
                        for sx in range(scale):
                            pix[oy + gy * scale + sy][ox + gx * scale + sx] = (255, 255, 255, 255)
    return F.tga(size, size, pix)


def screen(title, colour):
    """A 640x480 loading screen: a flat colour and a centred title."""
    w, h, scale = 640, 480, 6
    bg = rgb(colour)
    pix = [[bg] * w for _ in range(h)]
    x0 = (w - len(title) * 6 * scale) // 2
    y0 = (h - 7 * scale) // 2
    for i, c in enumerate(title):
        g = glyph(c)
        for gy, bits in enumerate(g):
            for gx in range(5):
                if bits & (0x10 >> gx):
                    for sy in range(scale):
                        for sx in range(scale):
                            pix[y0 + gy * scale + sy][x0 + (i * 6 + gx) * scale + sx] = \
                                (255, 255, 255, 255)
    return F.bmp(w, h, pix)


# ─── Models ──────────────────────────────────────────────────────────────────

def cube(w, d, h, z0=0.0):
    """A box w wide, d deep and h tall, standing on z0 and centred on the axis."""
    return F.mdl([F.box(-w / 2, -d / 2, z0, w / 2, d / 2, z0 + h)])


MODELS = {
    "player.mdl":    cube(0.40, 0.40, 0.70),
    "catcher.mdl":   cube(0.45, 0.45, 0.80),
    "thrower.mdl":   cube(0.55, 0.55, 0.45),
    "crystal.mdl":   F.mdl([F.sphere(0.16, 0.35)]),
    "item.mdl":      cube(0.35, 0.35, 0.35, 0.05),
    "bomb.mdl":      cube(0.25, 0.25, 0.25),
    "obstacle.mdl":  cube(0.90, 0.90, 0.70),
    "block.mdl":     cube(0.96, 0.96, 0.80),
    "lift.mdl":      cube(1.00, 1.00, 0.20, -0.20),
    "stair.mdl":     cube(1.00, 1.00, 0.50, -0.50),
    "falling.mdl":   cube(1.00, 1.00, 0.10, -0.10),
}


# ─── Particle systems ────────────────────────────────────────────────────────

PARTICLES = {
    # Bursts: the whole count at once (the default rate).
    "explosion": F.par(120, 0.35, [(0xffa040, 2), (0xffe080, 1), (0xff4020, 1)], (0.5, 0.1),
                       speed=(3.0, 0.5), spread=(30, 10, 30), fade_ms=400, floor=True),
    "crystalfx": F.par(24, 0.15, [(0x40ffff, 2), (0xffffff, 1)], (0.5, 0.1), speed=(4.0, 0.5),
                       spread=(1, 0.2, 1), pos=(0, 0.4, 0), gravity=10, fade_ms=250),
    "collect":   F.par(30, 0.15, [(0xffff60, 1), (0xff80ff, 1), (0x80ff80, 1)], (0.6, 0.2),
                       speed=(6.0, 0.5), spread=(0.3, 0.2, 0.3), pos=(0, 0.4, 0), gravity=20),
    "dust":      F.par(30, 0.25, [(0x909090, 2), (0x605040, 1)], (0.6, 0.15),
                       speed=(0.0, 0.5), spread=(1, 1, 1), box=((-0.45, -0.4, -0.45), (0.45, 0.05, 0.45)),
                       gravity=8, fade_ms=300),
    # Streams: a few a second, for as long as the object is there.
    "exit":      F.par(60, 0.12, [(0x60ff60, 2), (0xffffff, 1)], (1.0, 0.3), speed=(1.0, 0.2),
                       spread=(0.05, 0.1, 0.05), box=((-0.4, 0, -0.4), (0.4, 0.05, 0.4)), rate=40),
    "teleporter": F.par(40, 0.10, [(0xff60ff, 2), (0xffffff, 1)], (0.8, 0.2), speed=(1.5, 0.2),
                        spread=(0.02, 0.1, 0.02), box=((-0.3, 0, -0.3), (0.3, 0.05, 0.3)), rate=30),
    "fuse":      F.par(60, 0.08, [(0xffe080, 2), (0xff4020, 1)], (0.25, 0.05), speed=(1.5, 0.3),
                       spread=(1, 1, 1), gravity=2, rate=150, fade_ms=200),
    "speed":     F.par(80, 0.12, [(0xff8040, 1), (0xffe080, 1)], (0.6, 0.1),
                       pos_spread=(0.05, 0.05, 0.05), spread=(0.01, 0, 0.01), rate=50, fade_ms=150),
}


# ─── Textures ────────────────────────────────────────────────────────────────

def textures():
    t = {}
    # Opened by name in the code (render/renderstate.cpp, screens/menuscreens.cpp).
    # The shadow pass draws each model flattened onto the ground with this
    # texture on the model's own UVs, so it must be the same all over.
    t["textures/shadow.tga"] = solid(0x000000, 2, a=110)
    # The fonts' atlases (fonts/*.fon name them).
    t["textures/font2.tga"] = font_atlas(256, 16, 16, [chr(i) for i in range(256)], 2)
    t["textures/numbers.tga"] = font_atlas(128, 4, 3, list("0123456789:."), 4)

    # The HUD, menu panels and widgets (ui.py).
    t.update(ui.textures())

    b = "textures/basic/"
    t[b + "edge.tga"] = solid(0xffffff, 8)
    for name, colour in (("freeze", 0x80c0ff), ("inverse", 0xff80ff), ("protection", 0xffff80),
                         ("slow", 0x808080), ("speed", 0xff8040)):
        t[b + "bonus_%s.tga" % name] = ring(colour)
    for face in ("UP", "DN", "FR", "BK", "LF", "RT"):
        top, bottom = {"UP": (0x4080e0, 0x4080e0), "DN": (0x203040, 0x203040)}.get(
            face, (0x4080e0, 0xa0c0e0))
        t[b + "sky/sky_%s.tga" % face] = gradient(top, bottom, 64)

    # Floor tiles: a colour per kind, so a level reads at a glance.
    t[b + "plate.tga"] = tile(0xb0b0b0, 0x808080)
    t[b + "side.tga"] = tile(0x707070, 0x505050)
    t[b + "exit.tga"] = tile(0x30c030, 0x106010, mark=lambda u, v: abs(u - 0.5) + abs(v - 0.5) < 0.2)
    t[b + "exit_glow.tga"] = disc(0x60ff60, 64, soft=True)
    # Particles: white, tinted by each particle's colour, drawn additively.
    # Additive blending ignores alpha, so the glow fades in the colour too.
    t[b + "spark.tga"] = image(32, lambda u, v: (lambda a: (a, a, a, a))(
        int(255 * max(0.0, 1 - math.hypot(u - 0.5, v - 0.5) * 2) ** 2)))
    t[b + "block.tga"] = tile(0x8a6a4a, 0x5a3a1a, 32,
                              mark=lambda u, v: abs(u - v) < 0.06 or abs(u + v - 1) < 0.06)
    t[b + "glue.tga"] = tile(0x806030, 0x503010, mark=lambda u, v: (int(u * 8) + int(v * 8)) % 3 == 0)
    t[b + "falling.tga"] = tile(0xf0c020, 0x303030, mark=lambda u, v: int((u + v) * 4) % 2 == 0)
    t[b + "jumppad.tga"] = tile(0xe0e030, 0x808010, mark=lambda u, v: abs(math.hypot(u - .5, v - .5) - .25) < .05)
    t[b + "teleporter.tga"] = tile(0xc040c0, 0x601060, mark=lambda u, v: math.hypot(u - .5, v - .5) < .2)
    t[b + "slide.tga"] = tile(0x40c0c0, 0x106060, mark=lambda u, v: abs(v - 0.5) < 0.3 and abs(u - 0.5) < 0.05)
    t[b + "ice.tga"] = tile(0xd0f0ff, 0x80b0d0)
    t[b + "switch.tga"] = tile(0xc03030, 0x601010, mark=lambda u, v: math.hypot(u - .5, v - .5) < .15)
    t[b + "bridge.tga"] = tile(0xa06030, 0x603010, mark=lambda u, v: abs(v * 6 - round(v * 6)) < 0.1)
    # Object skins.
    for name, colour in (("player", 0x3070e0), ("player_dead", 0x808080), ("catcher", 0xe03030),
                         ("thrower", 0x902020), ("crystal", 0x40ffff), ("ammo", 0x404040),
                         ("bomb", 0x202020), ("freeze", 0x80c0ff), ("speed", 0xff8040),
                         ("life", 0xff4080), ("time", 0x4040ff), ("protection", 0xffff40),
                         ("paraglide", 0x40e040), ("obstacle", 0x907050), ("lift", 0x8080a0),
                         ("stair", 0x909090)):
        t[b + "%s.tga" % name] = tile(colour, max(0, colour - 0x303030) & 0xffffff, 32)
    return t


# ─── Sounds ──────────────────────────────────────────────────────────────────

# The theme's sound events (audio/themesounds.cpp) and what each one plays.
THEME_SOUNDS = {
    "MoveJJ":               "step.wav",
    "MoveCatcher":          "foe.wav",
    "MoveThrower":          "foe.wav",
    "MoveIceSliding":       "slide.wav",
    "MoveSliding":          "slide.wav",
    "MoveParagliding":      "slide.wav",
    "MoveParaglidingStart": "jump.wav",
    "MoveJumpPad":          "jump.wav",
    "Teleporter":           "teleport.wav",
    "Elevator":             "machine.wav",
    "Platform":             "machine.wav",
    "Switch":               "click.wav",
    "Glue":                 "glue.wav",
    "Bridge":               "machine.wav",
    "DestructStart":        "crack.wav",
    "DestructRegen":        "regen.wav",
    "Obstacle":             "crack.wav",
    "Crystal":              "crystal.wav",
    "SplatJJ":              "splat.wav",
    "SplatCatcher":         "splat.wav",
    "SplatThrower":         "splat.wav",
    "FallJJ":               "fall.wav",
    "FallCatcher":          "fall.wav",
    "FallThrower":          "fall.wav",
    "BombTick":             "tick.wav",
    "ExplosionBomb":        "boom.wav",
    "ExplosionCatcher":     "boom.wav",
}


def sounds():
    s = {}
    b = "waves/basic/"
    s[b + "step.wav"] = F.wav(F.tone(220, 0.08, 0.2))
    s[b + "foe.wav"] = F.wav(F.tone(110, 0.12, 0.2))
    s[b + "slide.wav"] = F.wav(F.tone(0, 0.3, 0.1, noise=True))
    s[b + "jump.wav"] = F.wav(F.tone(300, 0.2, slide=500))
    s[b + "teleport.wav"] = F.wav(F.tone(400, 0.4, slide=800))
    s[b + "machine.wav"] = F.wav(F.tone(80, 0.5, 0.3))
    s[b + "click.wav"] = F.wav(F.tone(1000, 0.03))
    s[b + "glue.wav"] = F.wav(F.tone(150, 0.3, slide=-80))
    s[b + "crack.wav"] = F.wav(F.tone(0, 0.25, 0.5, noise=True, seed=7))
    s[b + "regen.wav"] = F.wav(F.tone(200, 0.4, slide=300))
    s[b + "crystal.wav"] = F.wav(F.tone(880, 0.2))
    s[b + "splat.wav"] = F.wav(F.tone(0, 0.2, 0.6, noise=True, seed=3))
    s[b + "fall.wav"] = F.wav(F.tone(600, 0.8, slide=-500))
    s[b + "tick.wav"] = F.wav(F.tone(1500, 0.02))
    s[b + "boom.wav"] = F.wav(F.tone(0, 0.6, 0.8, noise=True, seed=11))

    # Opened by name in the code (sim/fixedsounds.cpp, app/launcherdialogs.cpp).
    w = "waves/"
    for i in range(1, 11):
        for bank in "ABC":
            s[w + "add%02d%s.wav" % (i, bank)] = F.wav(
                F.tone(440 * 2 ** ((i + "ABC".index(bank) * 2) / 12), 0.15))
    s[w + "TimeOut.wav"] = F.wav(F.tone(200, 0.8, slide=-100))
    s[w + "LastSeconds.wav"] = F.wav(F.tone(1200, 0.1))
    s[w + "Count.wav"] = F.wav(F.tone(660, 0.1))
    s[w + "MenuUpDown.wav"] = F.wav(F.tone(500, 0.04))
    s[w + "Switch.wav"] = F.wav(F.tone(1000, 0.03))
    s[w + "LevelCompleted.wav"] = F.wav(F.tone(523, 0.6, slide=523))
    s[w + "splat.wav"] = F.wav(F.tone(0, 0.2, 0.6, noise=True, seed=3))
    s[w + "mineimpact.wav"] = F.wav(F.tone(0, 0.3, 0.6, noise=True, seed=5))
    s[w + "ugh.wav"] = F.wav(F.tone(180, 0.3, slide=-60))
    return s


# ─── The theme file ──────────────────────────────────────────────────────────

def theme():
    b = "textures\\basic\\"
    m = "models\\basic\\"
    lines = ["// Basic: the placeholder theme (tools/gen_assets.py).", "", "Environment", "{"]
    for slot, name in (("HUD", "hud"), ("Menu", "menu"), ("Edge", "edge"), ("Pointer", "pointer"),
                       ("Radar", "radar"), ("Freeze", "bonus_freeze"),
                       ("InverseControl", "bonus_inverse"), ("Protection", "bonus_protection"),
                       ("Slowdwon", "bonus_slow"), ("Speed", "bonus_speed")):
        lines.append("\t%s\t%s%s.tga alpha" % (slot, b, name))
    for key in ("HUD", "MenuNewGame", "MenuLoadGame", "MenuHighscores", "MenuOptions",
                "MenuCredits", "MenuQuit", "MenuLoadGameEntries", "MenuSaveGameEntries",
                "MenuHighscoresEntries", "MenuOptionsControl", "MenuOptionsVideo",
                "MenuOptionsAudio", "MenuControlEntries", "MenuControlCamera",
                "MenuVideoReflection", "MenuVideoShadow", "MenuVideoHighlight",
                "MenuVideoParticle", "MenuAudio3DSound", "MenuAudioSoundVol",
                "MenuAudioCDMusic", "MenuAudioCDVol", "MenuSummaryEntries",
                "MenuSummaryNext", "MenuSummarySave"):
        lines.append("\t%sTextColors\tFFFFFF\tFFFF00" % key)
    lines.append("\tSky\t\t%ssky\\sky" % b)
    lines.append("\tSideHeight\t0.5")
    for event, wave in THEME_SOUNDS.items():
        lines.append("\tSound %s\twaves\\basic\\%s" % (event, wave))
    lines.append("}")

    def field(texture, extra=()):
        return ["\tField", "\t{", "\t\tTexture %s%s.tga" % (b, texture), *extra, "\t}"]

    def model(mdl, texture, extra=(), subextra=()):
        out = ["\tModel %s%s.mdl" % (m, mdl), "\t{", *("\t\t" + e for e in extra),
               "\t\tTexture %s%s.tga" % (b, texture)]
        if subextra:
            out += ["\t\t{", *("\t\t\t" + e for e in subextra), "\t\t}"]
        return out + ["\t}"]

    def particles(par, count=None, extra=(), subextra=(), movable=False):
        """A particle system record: drawn additively with the spark.  extra
        is the record's placement, subextra the texture's state (Condition)."""
        head = "\tParticleSystem %s%s.par %s" % (m, par, "movable" if movable else "notMovable")
        if count:
            head += " %d" % count
        return [head, "\t{", *("\t\t" + e for e in extra),
                "\t\tTexture %sspark.tga" % b, "\t\t{", "\t\t\tSrcBlend One",
                "\t\t\tDestBlend One", *("\t\t\t" + e for e in subextra), "\t\t}", "\t}"]

    explode = "Explode 2 0.1 200 0 0.4 0"
    spin = ("RandomYAngle", "Rotate 0 0.001 0")
    objects = {
        "John": model("player", "player", subextra=("Condition Alive",))
        + model("player", "player_dead", subextra=("Condition Dead",)),
        "Catcher": model("catcher", "catcher"),
        "CatcherFX": model("catcher", "catcher", extra=(explode,)),
        "Thrower": model("thrower", "thrower"),
        "ThrowerFX": model("thrower", "thrower", extra=(explode,)),
        "Plate": field("plate"),
        "Side": field("side", ("\t\t{", "\t\t\tTextureAdress Wrap", "\t\t}")),
        "Exit": field("exit", ("\t\tTexture %sexit_glow.tga" % b, "\t\t{", "\t\t\tSrcBlend One",
                               "\t\t\tDestBlend One", "\t\t\tCondition Active", "\t\t}"))
        + particles("exit", subextra=("Condition Active",)),
        "Glue": field("glue"),
        # A fallen tile is "active" until it is back: only then is it drawn.
        "DestructField": model("falling", "falling", subextra=("Condition InActive",)),
        "DestructFieldFX": particles("dust", 5),
        "JumpPad": field("jumppad"),
        "Teleporter": field("teleporter") + particles("teleporter"),
        "Slide": field("slide"),
        "Ice": field("ice"),
        "Switch": field("switch"),
        "Bridge": field("bridge"),
        "Stair": model("stair", "stair"),
        "Elevator": model("lift", "lift"),
        "Platform": model("lift", "lift"),
        "Crystal": model("crystal", "crystal", extra=spin + ("Oscillate 0.1 0.005 random", "Specular")),
        "CrystalFX": particles("crystalfx", 10),
        "CollFX": particles("collect", 10),
        "Ammunition": model("item", "ammo", extra=spin),
        "Freeze": model("item", "freeze", extra=spin),
        "Speed": model("item", "speed", extra=spin),
        "Life": model("item", "life", extra=spin),
        "Time": model("item", "time", extra=spin),
        "Protection": model("item", "protection", extra=spin),
        "Paraglide": model("item", "paraglide", extra=spin),
        "Surprise": model("item", "plate", extra=spin),
        "Bomb": model("bomb", "bomb") + particles("fuse", extra=("Position 0 0.3 0",)),
        "Explosion": model("bomb", "bomb", extra=(explode,)) + particles("explosion", 5),
        "SpeedFX": particles("speed", movable=True),
        "Obstacle": field("plate") + model("obstacle", "obstacle", subextra=("Condition InActive",)),
        "ObstacleFX": field("plate") + model("obstacle", "obstacle", extra=("Explode 3 0.3 200 0 0.3 0",)),
    }
    for name, body in objects.items():
        lines += ["", name, "{", *body, "}"]
    return ("\r\n".join(lines) + "\r\n").encode("ascii")


# ─── Everything ──────────────────────────────────────────────────────────────

def files():
    out = {}
    out.update(textures())
    out.update(sounds())
    for name, data in MODELS.items():
        out["models/basic/" + name] = data
    for name, data in PARTICLES.items():
        out["models/basic/%s.par" % name] = data
    out["themes/%s.thm" % THEME] = theme()
    out["fonts/FONT1.FON"] = F.fon("textures\\font2.tga", 16, 16)
    out["fonts/NUMBERS.FON"] = F.fon("textures\\numbers.tga", 4, 3)
    out["bitmaps/LOADING.BMP"] = screen("LOADING", 0x102040)
    out["bitmaps/%s.bmp" % THEME.upper()] = screen(THEME.upper(), 0x204060)
    out.update(levels.files(THEME))
    return out
