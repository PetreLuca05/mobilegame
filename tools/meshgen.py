#!/usr/bin/env python3
"""meshgen: mgmesh text -> engine mesh binary (build time, run by CMake).

Input (y-up, map units, flat-shaded triangle soup, counter-clockwise = outward):
    # comment
    c <index> <rrggbb>[:<zone>]         palette entry; zone 0..15 (decimal, default 0)
    g <name>                            starts a group
    t <colour> x y z x y z x y z        one triangle
    i <name> x y z [size sx sy sz]      instance (wheel positions, roof light boxes)
    i <name> x y z axis <x|y|z>         spinner: where a moving part sits and the axis it turns about

Output, little endian ("MGMB" version 2):
    "MGMB" | version u32 | group_count u32
    per group: name char[24] | vertex_count u32 | instance_count u32
               vertices: px py pz nx ny nz f32, r g b a u8 (28 bytes, GfxVertex), 3 per triangle
               instances: name char[16] | kind u32 (0 point, 1 box, 2 spinner) | x y z f32 |
                          sx sy sz f32 (box size; spinner: the unit axis; 0 for a point)
The vertex alpha byte carries the palette entry's zone as 255 - zone, so a mesh
without zones (every entry zone 0) keeps alpha 255. The engine reads the zone
to pick a per-instance tint or hide a detail (avatar suits: V-neck, coat, ...).
One vertex-coloured mesh per group, the normal of each triangle on its three
vertices. Any malformed line fails the build.

usage: meshgen.py IN.mgmesh OUT.mgmb
"""
import math
import struct
import sys


def fail(path, line_no, msg):
    sys.stderr.write(f"{path}:{line_no}: meshgen: {msg}\n")
    sys.exit(1)


def floats(path, n, parts, line_no):
    if len(parts) != n:
        fail(path, line_no, f"expected {n} numbers, got {len(parts)}")
    try:
        v = [float(p) for p in parts]
    except ValueError:
        fail(path, line_no, "not a number")
    if any(math.isnan(x) or math.isinf(x) for x in v):
        fail(path, line_no, "not a finite number")
    return v


def parse(path):
    palette = {}
    groups = []  # [name, [tris], [instances]]
    with open(path, "r", encoding="utf-8") as f:
        for line_no, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            kind = parts[0]
            if kind == "c":
                colour, _, zone_txt = parts[2].partition(":") if len(parts) == 3 else ("", "", "")
                if len(parts) != 3 or len(colour) != 6 or (":" in parts[2] and not zone_txt):
                    fail(path, line_no, "palette line is 'c <index> <rrggbb>[:<zone>]'")
                try:
                    idx = int(parts[1])
                    rgb = int(colour, 16)
                    zone = int(zone_txt, 10) if zone_txt else 0
                except ValueError:
                    fail(path, line_no, "bad palette index, colour or zone")
                if not 0 <= zone <= 15:
                    fail(path, line_no, f"zone {zone} is outside 0..15")
                palette[idx] = ((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255 - zone)
            elif kind == "g":
                if len(parts) != 2 or len(parts[1]) > 23:
                    fail(path, line_no, "group line is 'g <name>' (at most 23 chars)")
                if any(g[0] == parts[1] for g in groups):
                    fail(path, line_no, f"group {parts[1]} defined twice")
                groups.append([parts[1], [], []])
            elif kind == "t":
                if not groups:
                    fail(path, line_no, "triangle before any group")
                if len(parts) != 11:
                    fail(path, line_no, "triangle line is 't <colour> x y z x y z x y z'")
                try:
                    ci = int(parts[1])
                except ValueError:
                    fail(path, line_no, "bad colour index")
                if ci not in palette:
                    fail(path, line_no, f"colour {ci} is not in the palette")
                groups[-1][1].append((ci, floats(path, 9, parts[2:], line_no)))
            elif kind == "i":
                if not groups:
                    fail(path, line_no, "instance before any group")
                if len(parts) == 5:
                    kind, pos, size = 0, floats(path, 3, parts[2:5], line_no), [0.0, 0.0, 0.0]
                elif len(parts) == 9 and parts[5] == "size":
                    kind, pos, size = 1, floats(path, 3, parts[2:5], line_no), floats(path, 3, parts[6:9], line_no)
                elif len(parts) == 7 and parts[5] == "axis" and parts[6] in ("x", "y", "z"):
                    kind, pos = 2, floats(path, 3, parts[2:5], line_no)
                    size = [1.0 if parts[6] == a else 0.0 for a in ("x", "y", "z")]
                else:
                    fail(path, line_no, "instance line is 'i <name> x y z [size sx sy sz | axis x|y|z]'")
                if len(parts[1]) > 15:
                    fail(path, line_no, "instance name is at most 15 chars")
                groups[-1][2].append((parts[1], kind, pos, size))
            else:
                fail(path, line_no, f"unknown line kind '{kind}'")
    if not groups:
        fail(path, 0, "no groups")
    return palette, groups


def normal(v):
    ax, ay, az, bx, by, bz, cx, cy, cz = v
    ux, uy, uz = bx - ax, by - ay, bz - az
    wx, wy, wz = cx - ax, cy - ay, cz - az
    nx, ny, nz = uy * wz - uz * wy, uz * wx - ux * wz, ux * wy - uy * wx
    length = math.sqrt(nx * nx + ny * ny + nz * nz)
    if length < 1e-12:
        return 0.0, 1.0, 0.0  # degenerate: never seen, keep it lit from above
    return nx / length, ny / length, nz / length


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: meshgen.py IN.mgmesh OUT.mgmb\n")
        sys.exit(2)
    src, dst = sys.argv[1], sys.argv[2]
    palette, groups = parse(src)
    out = bytearray(b"MGMB")
    out += struct.pack("<II", 2, len(groups))
    for name, tris, insts in groups:
        out += struct.pack("<24sII", name.encode(), len(tris) * 3, len(insts))
        for ci, v in tris:
            n = normal(v)
            r, g, b, a = palette[ci]
            for k in range(3):
                out += struct.pack("<6f4B", v[3 * k], v[3 * k + 1], v[3 * k + 2], n[0], n[1], n[2], r, g, b, a)
        for iname, kind, pos, size in insts:
            out += struct.pack("<16sI6f", iname.encode(), kind, *pos, *size)
    with open(dst + ".tmp", "wb") as f:
        f.write(out)
    import os
    os.replace(dst + ".tmp", dst)


if __name__ == "__main__":
    main()
