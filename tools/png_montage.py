#!/usr/bin/env python3
"""png_montage: stack PNG screenshots into one picture (standard library only).

usage: png_montage.py OUT.png [--cols N] [--scale K] IN.png...
Inputs: 8-bit RGB / RGBA, non-interlaced (the game's screenshots). Each tile is downscaled by K (box filter),
laid out row-major in N columns on a white background.
"""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG")
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or inter:
                sys.exit(f"{path}: only 8-bit RGB/RGBA, non-interlaced")
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line[i] for i in range(stride) if i % bpp < 3) if bpp == 4 else bytes(line))
        prev = line
    return w, h, rows


def shrink(w, h, rows, k):
    ow, oh = w // k, h // k
    out = []
    for y in range(oh):
        line = bytearray(ow * 3)
        for x in range(ow):
            for ch in range(3):
                s = 0
                for dy in range(k):
                    r = rows[y * k + dy]
                    for dx in range(k):
                        s += r[(x * k + dx) * 3 + ch]
                line[x * 3 + ch] = s // (k * k)
        out.append(bytes(line))
    return ow, oh, out


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    out, cols, scale, ins = args[0], 1, 1, []
    i = 1
    while i < len(args):
        if args[i] == "--cols":
            cols, i = int(args[i + 1]), i + 2
        elif args[i] == "--scale":
            scale, i = int(args[i + 1]), i + 2
        else:
            ins.append(args[i])
            i += 1
    tiles = [shrink(*read_png(p), scale) for p in ins]
    tw, th = max(t[0] for t in tiles), max(t[1] for t in tiles)
    nrows = (len(tiles) + cols - 1) // cols
    canvas = [bytearray(b"\xff" * (tw * cols * 3)) for _ in range(th * nrows)]
    for n, (w, h, rows) in enumerate(tiles):
        ox, oy = (n % cols) * tw, (n // cols) * th
        for y in range(h):
            canvas[oy + y][ox * 3:(ox + w) * 3] = rows[y]
    write_png(out, tw * cols, th * nrows, [bytes(r) for r in canvas])


if __name__ == "__main__":
    main()
