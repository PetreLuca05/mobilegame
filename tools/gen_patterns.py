#!/usr/bin/env python3
"""The institute's detail textures, generated at build time (CMake) into one small embedded asset.

They are the reference page's ten procedural 256 x 256 near-white patterns (content/gastro's institute viewer: TEX =
tile, wood, paving, plaster, brick, asphalt, grass, gravel, water, hedge), drawn the way its 2D canvas draws them: an
8-bit grey canvas, fillRect with the rectangle's area coverage per pixel, filled circles and the water's strokes with a
one-pixel anti-aliased edge, source-over blending with globalAlpha, the 3 x 3 wrap. The random stream is the page's
(s = s * 16807 % 2147483647, seed 7), consumed in the same order (TEX's key order, then each call's argument order).

Output (little endian): "MGP1", u32 size (256), u32 layers (10), u32 levels (9), then per layer its whole mip chain
(256^2, 128^2 .. 1, box-filtered, tightly packed). game/inst_patterns.c uploads it as it is (GFX_MAT_PATTERN).

  python3 tools/gen_patterns.py OUT.bin [--png SHEET.png]
"""
import math
import struct
import sys


N = 256


class Canvas:
    """8-bit grey canvas (pure Python: no third-party module at build time)."""

    def __init__(self):
        self.px = bytearray([255]) * (N * N)
        self.fill = 1.0
        self.alpha = 1.0
        self.seed = 7

    def rnd(self):
        self.seed = self.seed * 16807 % 2147483647
        return self.seed / 2147483647.0

    def blend(self, x, y, cov):
        a = cov * self.alpha
        if a <= 0.0:
            return
        i = y * N + x
        v = self.px[i] / 255.0 * (1.0 - a) + self.fill * a
        self.px[i] = math.floor(v * 255.0 + 0.5)

    def fill_rect(self, x, y, w, h):
        x1, y1 = x + w, y + h
        ix0, iy0 = int(max(0.0, math.floor(x))), int(max(0.0, math.floor(y)))
        ix1, iy1 = int(min(float(N), math.ceil(x1))), int(min(float(N), math.ceil(y1)))
        cxs = [max(min(x1, px + 1) - max(x, px), 0.0) for px in range(ix0, ix1)]
        for py in range(iy0, iy1):
            cy = max(min(y1, py + 1) - max(y, py), 0.0)
            for k, px in enumerate(range(ix0, ix1)):
                self.blend(px, py, cy * cxs[k])

    def fill_circle(self, cx, cy, r):
        ix0, iy0 = int(max(0.0, math.floor(cx - r - 1))), int(max(0.0, math.floor(cy - r - 1)))
        ix1, iy1 = int(min(float(N), math.ceil(cx + r + 1))), int(min(float(N), math.ceil(cy + r + 1)))
        for py in range(iy0, iy1):
            for px in range(ix0, ix1):
                d = math.hypot(px + 0.5 - cx, py + 0.5 - cy)
                self.blend(px, py, min(1.0, max(0.0, r + 0.5 - d)))

    def clear(self, v):
        self.fill = grey(v)
        self.alpha = 1.0
        self.fill_rect(0, 0, N, N)

    def stroke(self, xy, w):
        ys = [p[1] for p in xy]
        py0, py1 = int(max(0.0, math.floor(min(ys) - w))), int(min(float(N), math.ceil(max(ys) + w)))
        segs = list(zip(xy, xy[1:]))
        for py in range(py0, py1):
            qy = py + 0.5
            for px in range(N):
                qx = px + 0.5
                d = 1e9
                for (ax, ay), (bx, by) in segs:
                    ex, ey = bx - ax, by - ay
                    l2 = ex * ex + ey * ey
                    t = ((qx - ax) * ex + (qy - ay) * ey) / l2 if l2 > 0.0 else 0.0
                    t = min(1.0, max(0.0, t))
                    d = min(d, math.hypot(qx - ax - ex * t, qy - ay - ey * t))
                cov = min(1.0, max(0.0, w * 0.5 + 0.5 - d))
                if cov > 0.0:
                    self.blend(px, py, cov)


def grey(v):  # g(v): 'rgb(n,n,n)', n = Math.round(v * 255)
    return math.floor(v * 255.0 + 0.5) / 255.0


def speck(c, n, lo, hi, sz):
    for _ in range(n):
        c.fill = grey(lo + c.rnd() * (hi - lo))
        px = c.rnd() * N
        py = c.rnd() * N
        w = sz * (0.5 + c.rnd())
        h = sz * (0.5 + c.rnd())
        for a in (-1, 0, 1):
            for b in (-1, 0, 1):
                ox, oy = a * N, b * N
                if px + ox < N and py + oy < N and px + ox + w > 0 and py + oy + h > 0:
                    c.fill_rect(px + ox, py + oy, w, h)


def blobs(c, n, lo, hi, r0, r1):
    for _ in range(n):
        c.fill = grey(lo + c.rnd() * (hi - lo))
        px = c.rnd() * N
        py = c.rnd() * N
        r = r0 + c.rnd() * r1
        for a in (-1, 0, 1):
            for b in (-1, 0, 1):
                ox, oy = a * N, b * N
                if -r < px + ox < N + r and -r < py + oy < N + r:
                    c.fill_circle(px + ox, py + oy, r)


def tile(c):
    speck(c, 500, .93, 1, 5)
    c.fill = grey(.74)
    for i in range(2):
        c.fill_rect(i * 128, 0, 3, 256)
        c.fill_rect(0, i * 128, 256, 3)
    c.fill = grey(.97)
    c.fill_rect(3, 3, 60, 2)


def wood(c):
    for r in range(4):
        off = (r % 2) * 90 + c.rnd() * 30
        for k in range(-1, 3):
            c.fill = grey(.84 + c.rnd() * .14)
            c.fill_rect(off + k * 128, r * 64, 128, 64)
            c.fill = grey(.7)
            c.fill_rect(off + k * 128, r * 64, 2, 64)
        for _ in range(10):
            c.fill = grey(.8 + c.rnd() * .1)
            c.fill_rect(0, r * 64 + c.rnd() * 60, 256, 1)
        c.fill = grey(.66)
        c.fill_rect(0, r * 64, 256, 2)


def paving(c):
    for r in range(4):
        for k in range(-1, 3):
            c.fill = grey(.82 + c.rnd() * .16)
            c.fill_rect((r % 2) * 64 + k * 128 + 2, r * 64 + 2, 124, 60)
    c.alpha = .5
    speck(c, 300, .8, 1, 3)
    c.alpha = 1


def plaster(c):
    speck(c, 900, .93, 1, 7)
    speck(c, 150, .9, .96, 2)


def brick(c):
    c.clear(.96)
    for r in range(8):
        for k in range(-1, 5):
            c.fill = grey(.68 + c.rnd() * .24)
            c.fill_rect((r % 2) * 32 + k * 64 + 2, r * 32 + 2, 60, 28)


def asphalt(c):
    c.clear(.9)
    speck(c, 2600, .74, 1, 2.2)


def grass(c):
    c.clear(.95)
    blobs(c, 90, .86, 1, 10, 26)
    blobs(c, 160, .84, .98, 3, 6)
    speck(c, 1400, .8, 1, 2)


def gravel(c):
    c.clear(.92)
    speck(c, 2200, .78, 1, 3)


def water(c):
    c.fill = grey(.86)  # strokeStyle, lineWidth 3
    for r in range(8):
        c.stroke([(i, r * 32 + 16 + math.sin(i / 256.0 * 6.283 * 2 + r) * 6) for i in range(0, 257, 8)], 3)


def hedge(c):
    c.clear(.92)
    blobs(c, 260, .74, 1, 5, 9)


DRAW = [tile, wood, paving, plaster, brick, asphalt, grass, gravel, water, hedge]


def generate():
    c = Canvas()
    layers = []
    for draw in DRAW:
        c.px = bytearray(N * N)
        c.clear(1.0)  # tex(): fillStyle '#fff', fillRect(0, 0, 256, 256)
        draw(c)
        layers.append(bytes(c.px))
    return layers


def mips(layer):
    out = [layer]
    s = N
    while s > 1:
        src, h = out[-1], s // 2
        dst = bytearray(h * h)
        for y in range(h):
            for x in range(h):
                i = 2 * y * s + 2 * x
                dst[y * h + x] = (src[i] + src[i + 1] + src[i + s] + src[i + s + 1] + 2) // 4
        out.append(bytes(dst))
        s = h
    return out


def main():
    out = sys.argv[1]
    layers = generate()
    chains = [mips(l) for l in layers]
    with open(out, 'wb') as f:
        f.write(b'MGP1' + struct.pack('<III', N, len(layers), len(chains[0])))
        for ch in chains:
            for m in ch:
                f.write(m)
    if '--png' in sys.argv:
        from PIL import Image  # optional, only for the picture
        sheet = Image.new('L', (5 * 260, 2 * 260), 255)
        for i, l in enumerate(layers):
            r, k = divmod(i, 5)
            sheet.paste(Image.frombytes('L', (N, N), l), (k * 260, r * 260))
        sheet.save(sys.argv[sys.argv.index('--png') + 1])


if __name__ == '__main__':
    main()
