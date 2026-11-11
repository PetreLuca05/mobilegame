#!/usr/bin/env python3
"""BOOK-ENGINE figures (decision 7): every fig/<id>.png next to a content pack -> one .mgfig file the game embeds and reads
in place (no PNG decoding at run time).

    python3 -I tools/book/figpack.py FIGDIR OUT.mgfig astc|rgba8

astc  = ASTC 4x4 (Apple and Android) through the OS's ImageIO (tools/book/astc_encode.swift; macOS only)
rgba8 = raw RGBA8 (desktop Linux), decoded here without Pillow

Format "MGFG" v1, little endian:
  header 16:  magic "MGFG", version u32 = 1, count u32, format u32 (0 rgba8, 1 astc 4x4)
  count entries of 48, sorted by id:  id char[24] (NUL padded), w u16, h u16, offset u32 (from the file start, 16-aligned),
                                      size u32, pad[12]
  data: rgba8 = w*h*4 bytes, top row first; astc = ceil(w/4)*ceil(h/4) blocks of 16 bytes, top-left block first
"""
import os, shutil, struct, subprocess, sys, tempfile, zlib

FMT = {'rgba8': 0, 'astc': 1}


def png_rgba(path):
    """8-bit non-interlaced PNG (grey, grey+alpha, RGB, RGBA, palette) -> (w, h, RGBA bytes, top row first)"""
    b = open(path, 'rb').read()
    if b[:8] != b'\x89PNG\r\n\x1a\n':
        sys.exit(f'figpack: {path} is not a PNG')
    pos, idat, plte, trns = 8, b'', None, None
    while pos < len(b):
        n, t = struct.unpack('>I4s', b[pos:pos + 8])
        d = b[pos + 8:pos + 8 + n]
        if t == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', d)
        elif t == b'PLTE':
            plte = d
        elif t == b'tRNS':
            trns = d
        elif t == b'IDAT':
            idat += d
        pos += 12 + n
    if depth != 8 or inter:
        sys.exit(f'figpack: {path}: only 8-bit non-interlaced PNGs (depth {depth}, interlace {inter})')
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw, stride = zlib.decompress(idat), w * ch
    out, prev = bytearray(), bytearray(stride)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - ch] if i >= ch else 0
            c = prev[i - ch] if i >= ch else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + prev[i]) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
            elif f == 4:
                p = a + prev[i] - c
                pa, pb, pc = abs(p - a), abs(p - prev[i]), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else prev[i] if pb <= pc else c)) & 255
        out += line
        prev = line
    rgba = bytearray(w * h * 4)
    for i in range(w * h):
        if ctype == 6:
            rgba[i * 4:i * 4 + 4] = out[i * 4:i * 4 + 4]
        elif ctype == 2:
            rgba[i * 4:i * 4 + 4] = out[i * 3:i * 3 + 3] + b'\xff'
        elif ctype == 0:
            rgba[i * 4:i * 4 + 4] = bytes((out[i],) * 3) + b'\xff'
        elif ctype == 4:
            rgba[i * 4:i * 4 + 4] = bytes((out[i * 2],) * 3) + out[i * 2 + 1:i * 2 + 2]
        else:
            k = out[i]
            rgba[i * 4:i * 4 + 4] = plte[k * 3:k * 3 + 3] + bytes((trns[k] if trns and k < len(trns) else 255,))
    return w, h, bytes(rgba)


def main(figdir, out, fmt):
    if fmt not in FMT:
        sys.exit('figpack: format is astc or rgba8')
    names = sorted(f for f in os.listdir(figdir) if f.endswith('.png'))
    blobs = []
    tmp = tempfile.mkdtemp(prefix='figpack_')
    try:
        if fmt == 'astc':
            swift = shutil.which('swift')
            if not swift:
                sys.exit('figpack: astc needs swift (Xcode command line tools) for ImageIO')
            args = []
            for n in names:
                args += [os.path.join(figdir, n), os.path.join(tmp, n[:-4] + '.astc')]
            enc = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'astc_encode.swift')
            # a Mac tool even inside an iOS build: Xcode's environment points SDKROOT etc. at the iPhone SDK
            env = {k: v for k, v in os.environ.items() if k not in ('SDKROOT', 'PLATFORM_NAME', 'EFFECTIVE_PLATFORM_NAME') and 'DEPLOYMENT_TARGET' not in k}
            xcrun = shutil.which('xcrun')
            cmd = [xcrun, '--sdk', 'macosx', 'swift'] if xcrun else [swift]
            subprocess.run(cmd + [enc] + args, check=True, env=env)
        for n in names:
            fid = n[:-4]
            if len(fid.encode()) > 23:
                sys.exit(f'figpack: id {fid} longer than 23 bytes')
            if fmt == 'astc':
                d = open(os.path.join(tmp, fid + '.astc'), 'rb').read()
                if d[:4] != b'\x13\xab\xa1\x5c' or d[4:7] != b'\x04\x04\x01':
                    sys.exit(f'figpack: {fid}: not an ASTC 4x4 file')
                w = int.from_bytes(d[7:10], 'little')
                h = int.from_bytes(d[10:13], 'little')
                data = d[16:]
                if len(data) != ((w + 3) // 4) * ((h + 3) // 4) * 16:
                    sys.exit(f'figpack: {fid}: {len(data)} bytes of blocks for {w}x{h}')
            else:
                w, h, data = png_rgba(os.path.join(figdir, n))
            blobs.append((fid, w, h, data))
    finally:
        shutil.rmtree(tmp)
    off = 16 + 48 * len(blobs)
    head, body = bytearray(struct.pack('<4sIII', b'MGFG', 1, len(blobs), FMT[fmt])), bytearray()
    for fid, w, h, data in blobs:
        off = (off + 15) & ~15
        pad = off - (16 + 48 * len(blobs) + len(body))
        body += b'\0' * pad
        head += struct.pack('<24sHHII12x', fid.encode(), w, h, off, len(data))
        body += data
        off += len(data)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, 'wb').write(bytes(head + body))
    big = max((len(b[3]) for b in blobs), default=0)
    print(f'figpack: {len(blobs)} figures, {fmt}, {len(head) + len(body):,} bytes, largest {big:,} bytes -> {out}')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
