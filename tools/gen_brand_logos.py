#!/usr/bin/env python3
"""The sign-in logos as two 128 x 128 RGBA cells (straight alpha) for the head-icon texture's reserved cells 62 and 63.

Usage: python3 tools/gen_brand_logos.py   (Pillow)   -> assets/brand/brand_cells.rgba (2 x 128 x 128 x 4 bytes)
Cell 0: Google's standard "G" (assets/brand/google_g_logo.png, Google's sign-in branding page), scaled to fit, centred,
unmodified otherwise. Cell 1: Apple's logo-only Sign in with Apple artwork (assets/brand/apple_logo_white.png, from Apple
Design Resources) when it is there, else empty (the game then draws no Apple mark). Provenance: docs/LICENSES.md.
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
CELL = 128


def cell_of(path):
    out = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    if not path.exists():
        return out
    im = Image.open(path).convert("RGBA")
    s = min(CELL / im.width, CELL / im.height)
    w, h = max(1, round(im.width * s)), max(1, round(im.height * s))
    out.paste(im.resize((w, h), Image.LANCZOS), ((CELL - w) // 2, (CELL - h) // 2))
    return out


def main():
    cells = [cell_of(ROOT / "assets/brand/google_g_logo.png"), cell_of(ROOT / "assets/brand/apple_logo_white.png")]
    data = b"".join(c.tobytes() for c in cells)
    (ROOT / "assets/brand/brand_cells.rgba").write_bytes(data)
    print(f"wrote assets/brand/brand_cells.rgba ({len(data)} bytes)")


if __name__ == "__main__":
    main()
