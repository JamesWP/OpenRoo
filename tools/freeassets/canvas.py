"""A small 2D canvas for the interface art: shapes as signed distances,
composited in order with a one-pixel soft edge.

A shape is a function (x, y) -> distance in pixels, negative inside.  The
coordinates are pixel centres, (0.5, 0.5) the top-left pixel's.
"""
import math

from . import formats as F
from .font5x7 import glyph


class Canvas:
    def __init__(self, width, height):
        self.w, self.h = width, height
        self.px = [[(0.0, 0.0, 0.0, 0.0)] * width for _ in range(height)]

    def fill(self, shape, colour, alpha=1.0, box=None):
        """Paints colour (0xRRGGBB) over shape.  box (x0, y0, x1, y1) limits
        the pixels visited; the whole canvas by default."""
        r, g, b = ((colour >> 16) & 255) / 255, ((colour >> 8) & 255) / 255, (colour & 255) / 255
        x0, y0, x1, y1 = box or (0, 0, self.w, self.h)
        for y in range(max(0, int(y0)), min(self.h, int(math.ceil(y1)))):
            row = self.px[y]
            for x in range(max(0, int(x0)), min(self.w, int(math.ceil(x1)))):
                a = max(0.0, min(1.0, 0.5 - shape(x + 0.5, y + 0.5))) * alpha
                if a <= 0:
                    continue
                pr, pg, pb, pa = row[x]
                oa = a + pa * (1 - a)
                row[x] = ((r * a + pr * pa * (1 - a)) / oa, (g * a + pg * pa * (1 - a)) / oa,
                          (b * a + pb * pa * (1 - a)) / oa, oa)

    def text(self, s, cx, cy, scale, colour, outline=None):
        """s centred on (cx, cy) in the 5x7 font, each font pixel scale
        pixels; outline draws a one-font-pixel border in that colour first."""
        width = len(s) * 6 * scale - scale
        left, top = cx - width / 2, cy - 7 * scale / 2
        cells = []
        for i, ch in enumerate(s):
            for gy, bits in enumerate(glyph(ch) or ()):
                for gx in range(5):
                    if bits & (0x10 >> gx):
                        cells.append((left + (i * 6 + gx) * scale, top + gy * scale))
        for col, grow in ((outline, scale * 0.6), (colour, 0)) if outline is not None else ((colour, 0),):
            for x, y in cells:
                self.fill(rect(x - grow, y - grow, x + scale + grow, y + scale + grow, grow * 0.8), col,
                          box=(x - grow - 1, y - grow - 1, x + scale + grow + 1, y + scale + grow + 1))

    def tga(self):
        return F.tga(self.w, self.h, [[(int(r * 255 + .5), int(g * 255 + .5), int(b * 255 + .5),
                                        int(a * 255 + .5)) for r, g, b, a in row] for row in self.px])


# ─── Shapes ──────────────────────────────────────────────────────────────────

def circle(cx, cy, r):
    return lambda x, y: math.hypot(x - cx, y - cy) - r


def ring(cx, cy, r0, r1):
    return lambda x, y: abs(math.hypot(x - cx, y - cy) - (r0 + r1) / 2) - (r1 - r0) / 2


def rect(x0, y0, x1, y1, radius=0.0):
    """An axis-aligned rectangle with corners rounded by radius."""
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - radius, (y1 - y0) / 2 - radius

    def d(x, y):
        qx, qy = abs(x - cx) - hx, abs(y - cy) - hy
        return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - radius
    return d


def segment(ax, ay, bx, by, width):
    """A line from a to b, width wide with round ends."""
    def d(x, y):
        px, py, vx, vy = x - ax, y - ay, bx - ax, by - ay
        t = max(0.0, min(1.0, (px * vx + py * vy) / (vx * vx + vy * vy)))
        return math.hypot(px - vx * t, py - vy * t) - width / 2
    return d


def polygon(points):
    """A convex polygon, its points in either order."""
    def d(x, y):
        best = -1e9
        n = len(points)
        sign = 1 if sum((points[i][0] * points[(i + 1) % n][1] - points[(i + 1) % n][0] * points[i][1])
                        for i in range(n)) > 0 else -1
        for i in range(n):
            (ax, ay), (bx, by) = points[i], points[(i + 1) % n]
            ex, ey = bx - ax, by - ay
            ln = math.hypot(ex, ey)
            best = max(best, sign * ((x - ax) * ey - (y - ay) * ex) / ln)
        return best
    return d


def arc(cx, cy, r0, r1, a0, a1):
    """A band between radii r0 and r1, from angle a0 to a1 (radians, 0 up,
    clockwise), with square ends."""
    band = ring(cx, cy, r0, r1)

    def d(x, y):
        a = math.atan2(x - cx, cy - y)
        if a0 <= a <= a1:
            return band(x, y)
        return max(band(x, y), min(abs(a - a0), abs(a - a1)) * math.hypot(x - cx, y - cy))
    return d
