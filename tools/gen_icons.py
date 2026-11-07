#!/usr/bin/env python3
"""Render the placeholder app icon (toon head on sky gradient) and write the
Android mipmaps and the iOS AppIcon asset catalog. Needs Pillow.

Usage: python3 tools/gen_icons.py
"""
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
S = 1024


def render(size=S):
    img = Image.new("RGB", (size, size))
    top, bottom = (0x3F, 0x8F, 0xE8), (0xBF, 0xE3, 0xFF)
    d = ImageDraw.Draw(img)
    for y in range(size):
        t = y / (size - 1)
        d.line([(0, y), (size, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bottom)))
    u = size / 1024.0

    def ell(cx, cy, rx, ry, fill):
        d.ellipse([(cx - rx) * u, (cy - ry) * u, (cx + rx) * u, (cy + ry) * u], fill=fill)

    # grass hill
    ell(512, 1180, 760, 330, (0x3D, 0xA8, 0x3A))
    # soft shadow
    shadow = Image.new("L", (size, size), 0)
    ImageDraw.Draw(shadow).ellipse([300 * u, 850 * u, 724 * u, 930 * u], fill=110)
    shadow = shadow.filter(ImageFilter.GaussianBlur(18 * u))
    img.paste((20, 60, 30), mask=shadow)
    d = ImageDraw.Draw(img)
    # body + head + hair
    ell(512, 760, 175, 120, (0xF2, 0x6B, 0x3A))
    ell(512, 470, 245, 245, (0xFF, 0xD3, 0xB0))
    d.chord([267 * u, 225 * u, 757 * u, 715 * u], 180, 360, fill=(0x5A, 0x3A, 0x22))
    ell(512, 470, 245, 30, (0xFF, 0xD3, 0xB0))
    # face
    ell(430, 500, 26, 38, (0x22, 0x22, 0x2A))
    ell(594, 500, 26, 38, (0x22, 0x22, 0x2A))
    d.rounded_rectangle([462 * u, 590 * u, 562 * u, 606 * u], radius=8 * u, fill=(0x22, 0x22, 0x2A))
    # hands + ball
    ell(300, 760, 52, 52, (0xFF, 0xD3, 0xB0))
    ell(724, 730, 52, 52, (0xFF, 0xD3, 0xB0))
    ell(770, 660, 80, 80, (0xFF, 0x8A, 0x1E))
    d.arc([690 * u, 580 * u, 850 * u, 740 * u], 200, 340, fill=(0x22, 0x22, 0x22), width=int(8 * u))
    d.line([770 * u, 580 * u, 770 * u, 740 * u], fill=(0x22, 0x22, 0x22), width=int(8 * u))
    return img


def main():
    big = render()
    (ROOT / "assets").mkdir(exist_ok=True)
    big.save(ROOT / "assets/icon_1024.png")

    res = ROOT / "platform/android/app/src/main/res"
    for name, px in {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}.items():
        out = res / f"mipmap-{name}"
        out.mkdir(parents=True, exist_ok=True)
        big.resize((px, px), Image.LANCZOS).save(out / "ic_launcher.png")

    cat = ROOT / "platform/ios/Assets.xcassets"
    icon_set = cat / "AppIcon.appiconset"
    icon_set.mkdir(parents=True, exist_ok=True)
    big.save(icon_set / "icon_1024.png")
    (cat / "Contents.json").write_text(json.dumps({"info": {"author": "xcode", "version": 1}}, indent=2) + "\n")
    (icon_set / "Contents.json").write_text(json.dumps({
        "images": [{"filename": "icon_1024.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"}],
        "info": {"author": "xcode", "version": 1},
    }, indent=2) + "\n")
    print("icons written")


if __name__ == "__main__":
    main()
