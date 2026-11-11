#!/usr/bin/env python3
"""png_diff: the largest per-channel difference between two same-size PNGs and how many pixels differ at all.
usage: png_diff.py A.png B.png"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from png_montage import read_png  # noqa: E402


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    wa, ha, ra = read_png(sys.argv[1])
    wb, hb, rb = read_png(sys.argv[2])
    if (wa, ha) != (wb, hb):
        sys.exit(f"sizes differ: {wa}x{ha} vs {wb}x{hb}")
    worst, differ = 0, 0
    for la, lb in zip(ra, rb):
        for x in range(0, len(la), 3):
            d = max(abs(la[x] - lb[x]), abs(la[x + 1] - lb[x + 1]), abs(la[x + 2] - lb[x + 2]))
            if d:
                differ += 1
                worst = max(worst, d)
    print(f"max {worst} / 255, {differ} of {wa * ha} pixels differ ({100.0 * differ / (wa * ha):.3f} %)")


if __name__ == "__main__":
    main()
