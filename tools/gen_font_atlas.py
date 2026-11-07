#!/usr/bin/env python3
"""Generate the single-channel SDF font atlas used by engine/ui/text.c.

Usage: python3 tools/gen_font_atlas.py [font.ttf] [out.mgf]

Needs Pillow, numpy and scipy (pip install pillow numpy scipy).
Each glyph is rasterised at 8x the atlas size, a signed Euclidean distance
transform is computed, and the result is downsampled into the atlas. The
atlas also carries a fully-inside "solid" cell (for UI rectangles) and an
analytic circle cell (for round buttons / joystick).

File format (little endian), parsed by text.c:
  char[4] 'MGF1'
  u16 atlas_w, atlas_h, glyph_count, reserved
  f32 em_px, ascent, descent, line_height, sdf_range_px
  u16 solid_x, solid_y, circle_x, circle_y, circle_size, reserved
  glyph_count x { u32 codepoint; u16 x, y, w, h; f32 xoff, yoff, advance }
  u8 pixels[atlas_w * atlas_h]
"""
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

ROOT = Path(__file__).resolve().parent.parent
FONT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "assets/fonts/Fredoka.ttf"
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "assets/font_atlas.mgf"

EM_PX = 40           # glyph em size in atlas pixels
UPSCALE = 8          # supersampling factor for the distance transform
RANGE_PX = 5.0       # distance (atlas px) mapped to the 0..1 value span
PAD = 6              # atlas px padding around each glyph
ATLAS_W, ATLAS_H = 512, 512
CIRCLE = 64
CHARS = [chr(c) for c in range(32, 127)] + ["°", "•"]
# Procedural icons (not in the font)
ICON_GEAR = 0x2699
ICON_ARROW_UP = 0x2191


def load_font(size):
    font = ImageFont.truetype(str(FONT), size)
    try:
        font.set_variation_by_axes([600, 100])  # Fredoka axes: Weight, Width
    except (OSError, ValueError):
        pass
    return font


def glyph_sdf(font, ch):
    """Return (sdf uint8 array, xoff, yoff, advance) in atlas pixels."""
    hi_em = EM_PX * UPSCALE
    advance = font.getlength(ch) / UPSCALE
    bbox = font.getbbox(ch, anchor="ls")  # relative to baseline origin
    if bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
        return None, 0.0, 0.0, advance
    pad_hi = PAD * UPSCALE
    w = bbox[2] - bbox[0] + 2 * pad_hi
    h = bbox[3] - bbox[1] + 2 * pad_hi
    # round up to a multiple of UPSCALE so downsampling is exact
    w = (w + UPSCALE - 1) // UPSCALE * UPSCALE
    h = (h + UPSCALE - 1) // UPSCALE * UPSCALE
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((pad_hi - bbox[0], pad_hi - bbox[1]), ch,
                             font=font, fill=255, anchor="ls")
    inside = np.asarray(img) > 127
    d_out = ndimage.distance_transform_edt(~inside)
    d_in = ndimage.distance_transform_edt(inside)
    sd = (d_in - d_out) / UPSCALE  # +inside, atlas px
    # sample at the centre of each atlas texel
    sd = sd[UPSCALE // 2::UPSCALE, UPSCALE // 2::UPSCALE]
    val = np.clip(0.5 + sd / (2.0 * RANGE_PX), 0.0, 1.0)
    xoff = (bbox[0] - pad_hi) / UPSCALE
    yoff = (bbox[1] - pad_hi) / UPSCALE  # negative = above baseline
    return (val * 255.0 + 0.5).astype(np.uint8), xoff, yoff, advance


def draw_gear(d, s):
    """Gear in an s x s box (white on black)."""
    import math
    cx = cy = s / 2
    r_out, r_body, r_hole = s * 0.47, s * 0.36, s * 0.15
    teeth, half_w = 8, s * 0.085
    for i in range(teeth):
        a = 2 * math.pi * i / teeth
        ca, sa = math.cos(a), math.sin(a)
        pts = []
        for (r, w) in ((r_body * 0.9, -half_w), (r_out, -half_w * 0.8), (r_out, half_w * 0.8), (r_body * 0.9, half_w)):
            pts.append((cx + ca * r - sa * w, cy + sa * r + ca * w))
        d.polygon(pts, fill=255)
    d.ellipse([cx - r_body, cy - r_body, cx + r_body, cy + r_body], fill=255)
    d.ellipse([cx - r_hole, cy - r_hole, cx + r_hole, cy + r_hole], fill=0)


def draw_arrow_up(d, s):
    head = [(s * 0.5, s * 0.08), (s * 0.92, s * 0.52), (s * 0.66, s * 0.52), (s * 0.66, s * 0.92),
            (s * 0.34, s * 0.92), (s * 0.34, s * 0.52), (s * 0.08, s * 0.52)]
    d.polygon(head, fill=255)


def icon_sdf(draw_fn):
    """SDF for a square icon one em tall sitting on the baseline."""
    em_hi = EM_PX * UPSCALE
    pad_hi = PAD * UPSCALE
    size = em_hi + 2 * pad_hi
    img = Image.new("L", (size, size), 0)
    sub = Image.new("L", (em_hi, em_hi), 0)
    draw_fn(ImageDraw.Draw(sub), em_hi)
    img.paste(sub, (pad_hi, pad_hi))
    inside = np.asarray(img) > 127
    sd = (ndimage.distance_transform_edt(inside) - ndimage.distance_transform_edt(~inside)) / UPSCALE
    sd = sd[UPSCALE // 2::UPSCALE, UPSCALE // 2::UPSCALE]
    val = np.clip(0.5 + sd / (2.0 * RANGE_PX), 0.0, 1.0)
    xoff = -PAD
    yoff = -EM_PX * 0.85 - PAD  # top of the icon a bit above cap height
    return (val * 255.0 + 0.5).astype(np.uint8), float(xoff), float(yoff), float(EM_PX * 1.05)


def main():
    font = load_font(EM_PX * UPSCALE)
    ascent, descent = font.getmetrics()
    atlas = np.zeros((ATLAS_H, ATLAS_W), dtype=np.uint8)

    # solid cell (8x8 fully inside) and circle cell in the top-left corner
    atlas[0:8, 0:8] = 255
    solid = (4, 4)
    yy, xx = np.mgrid[0:CIRCLE, 0:CIRCLE] + 0.5
    radius = CIRCLE / 2 - RANGE_PX - 1
    dist = radius - np.hypot(xx - CIRCLE / 2, yy - CIRCLE / 2)
    circle = np.clip(0.5 + dist / (2.0 * RANGE_PX), 0, 1)
    atlas[0:CIRCLE, 8:8 + CIRCLE] = (circle * 255 + 0.5).astype(np.uint8)
    circle_pos = (8, 0)

    glyphs = []
    x, y, row_h = 8 + CIRCLE + 2, 0, CIRCLE
    items = [(ord(ch), lambda ch=ch: glyph_sdf(font, ch)) for ch in CHARS]
    items += [(ICON_GEAR, lambda: icon_sdf(draw_gear)), (ICON_ARROW_UP, lambda: icon_sdf(draw_arrow_up))]
    for cp, make in items:
        sdf, xoff, yoff, adv = make()
        if sdf is None:
            glyphs.append((cp, 0, 0, 0, 0, 0.0, 0.0, adv))
            continue
        h, w = sdf.shape
        if x + w > ATLAS_W:
            x, y, row_h = 0, y + row_h + 2, 0
        if y + h > ATLAS_H:
            raise SystemExit("atlas too small")
        atlas[y:y + h, x:x + w] = sdf
        glyphs.append((cp, x, y, w, h, xoff, yoff, adv))
        x += w + 2
        row_h = max(row_h, h)

    out = bytearray()
    out += b"MGF1"
    out += struct.pack("<4H", ATLAS_W, ATLAS_H, len(glyphs), 0)
    out += struct.pack("<5f", EM_PX, ascent / UPSCALE, descent / UPSCALE,
                       (ascent + descent) / UPSCALE * 1.1, RANGE_PX)
    out += struct.pack("<6H", solid[0], solid[1], circle_pos[0], circle_pos[1],
                       CIRCLE, 0)
    for g in glyphs:
        out += struct.pack("<I4H3f", *g)
    out += atlas.tobytes()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(bytes(out))
    Image.fromarray(atlas).save(OUT.with_suffix(".preview.png"))
    print(f"wrote {OUT} ({len(out)} bytes, {len(glyphs)} glyphs, rows used {y + row_h}px)")


if __name__ == "__main__":
    main()
