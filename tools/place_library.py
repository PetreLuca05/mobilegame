#!/usr/bin/env python3
"""place_library: choose the library spot on every island of the world map (LIB-1 section 6).

usage: python3 -I tools/place_library.py [--dry-run]

For each book in assets/world/books.json, reads <book>.island.json (mg-island 2) and the
island's terrain mesh (<book>_island.mgmesh, group "terrain"), then picks

    library {x, z, yaw_deg} = the grid point (0.5-unit steps over the terrain bounds)
    nearest the island's `start` (ties: smaller x, then smaller z) that is
      - >= 30 units from every hospital plot centre (levels[].hospital {x, z}),
      - >= 8 units from the road polyline (`road`),
      - on the grass plateau: at the point and at every sample of the 13 x 10.5 pad
        (1-unit grid + corners, oriented with the library) the highest terrain surface is
        a flat triangle at the plateau level (ground_y .. ground_y + 0.06: the 2.2 grass cap
        and the 2.25 meadow patches on it); beach (1.15 / 1.2), slopes and water fail,
        and so does anything standing on the terrain (lighthouse, boats),
      - (added to the rule) the pad sits on bare grass: no triangle of the "scenery" group
        (road, ambulance start pad, ponds, fields, mounds, hedges, the hospital plots)
        covers any pad sample in plan view; the point the literal rule alone would give is
        printed too,
    facing (+z of the building) the nearest point of the road polyline:
    yaw_deg 0 faces +z, 90 faces +x, i.e. front = (sin yaw, cos yaw).

Then it removes every prop instance (props.<group>[] = [x, y, z, scale, yaw]) within 9 units
(x/z distance) of the point and writes, after "levels":

    "library":      {"x", "z", "yaw_deg"}
    "library_pad":  {"x", "z", "yaw_deg", "w": 13, "d": 10.5, "y", "corners": [[x, z] x 4]}
                    w along the library's local x, d along its local z (the front edge is
                    local z = +d/2); y = highest plateau surface under it; corners in world x/z,
                    in local terms (-w/2, +d/2) (-w/2, -d/2) (+w/2, -d/2) (+w/2, +d/2);
                    local -> world: x' = x + lx cos(yaw) + lz sin(yaw),
                                    z' = z - lx sin(yaw) + lz cos(yaw)
    "library_path": {"from": [x, z], "to": [x, z], "width": 3.4, "y"} (y = the pad's y)
                    a straight strip from the middle of the pad's front edge to the nearest
                    road point (on the road centre line; the road mesh covers the last
                    road_half_width units)

The JSON is rewritten compactly exactly like the existing files (no whitespace, UTF-8,
no trailing newline). Running it again gives the same spot and removes nothing more.
"""
import json
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

STEP = 0.5
MIN_HOSPITAL = 30.0
MIN_ROAD = 8.0
PROP_CLEAR = 9.0
PAD_W = 13.0
PAD_D = 10.5
PATH_W = 3.4
PLATEAU_TOL = 0.06   # accepted plateau surfaces: ground_y - 0.01 .. ground_y + PLATEAU_TOL
FLAT_NY = 0.995      # a flat triangle's normal y
CELL = 8.0           # spatial hash cell for triangles


def r2(v):
    v = round(v, 2)
    if v == int(v):
        return int(v)
    return v + 0.0 if v != 0 else 0


def load_mesh(path):
    """Returns {group: [(colour, (x0,y0,z0,x1,y1,z1,x2,y2,z2))]}."""
    groups = {}
    cur = None
    with open(path, "r", encoding="utf-8") as f:
        for raw in f:
            p = raw.split()
            if not p or p[0].startswith("#"):
                continue
            if p[0] == "g":
                cur = groups.setdefault(p[1], [])
            elif p[0] == "t" and cur is not None:
                cur.append((int(p[1]), tuple(float(x) for x in p[2:11])))
    return groups


class TriGrid:
    """Non-vertical triangles projected onto x/z, hashed by cell, with height lookup."""

    def __init__(self, tris):
        self.cells = {}
        self.tris = []
        self.minx = self.minz = 1e30
        self.maxx = self.maxz = -1e30
        for col, v in tris:
            x0, y0, z0, x1, y1, z1, x2, y2, z2 = v
            den = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2)
            if abs(den) < 1e-9:
                continue  # vertical in plan view: never the surface on top
            ux, uy, uz = x1 - x0, y1 - y0, z1 - z0
            vx, vy, vz = x2 - x0, y2 - y0, z2 - z0
            nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
            nl = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
            t = (v, den, col, abs(ny) / nl, max(y0, y1, y2))
            idx = len(self.tris)
            self.tris.append(t)
            ax0, ax1 = min(x0, x1, x2), max(x0, x1, x2)
            az0, az1 = min(z0, z1, z2), max(z0, z1, z2)
            self.minx, self.maxx = min(self.minx, ax0), max(self.maxx, ax1)
            self.minz, self.maxz = min(self.minz, az0), max(self.maxz, az1)
            for cx in range(math.floor(ax0 / CELL), math.floor(ax1 / CELL) + 1):
                for cz in range(math.floor(az0 / CELL), math.floor(az1 / CELL) + 1):
                    self.cells.setdefault((cx, cz), []).append(idx)

    def hits(self, x, z):
        """Yields (height, colour, flatness, max_y) for every triangle over (x, z)."""
        for idx in self.cells.get((math.floor(x / CELL), math.floor(z / CELL)), ()):
            v, den, col, flat, ymax = self.tris[idx]
            x0, y0, z0, x1, y1, z1, x2, y2, z2 = v
            a = ((z1 - z2) * (x - x2) + (x2 - x1) * (z - z2)) / den
            b = ((z2 - z0) * (x - x2) + (x0 - x2) * (z - z2)) / den
            c = 1.0 - a - b
            if a < -1e-6 or b < -1e-6 or c < -1e-6:
                continue
            yield a * y0 + b * y1 + c * y2, col, flat, ymax

    def top(self, x, z):
        best = None
        for h in self.hits(x, z):
            if best is None or h[0] > best[0]:
                best = h
        return best


def seg_closest(px, pz, ax, az, bx, bz):
    dx, dz = bx - ax, bz - az
    l2 = dx * dx + dz * dz
    t = 0.0 if l2 < 1e-12 else max(0.0, min(1.0, ((px - ax) * dx + (pz - az) * dz) / l2))
    return ax + dx * t, az + dz * t


def road_nearest(road, x, z):
    best = (1e30, 0.0, 0.0)
    for i in range(len(road) - 1):
        cx, cz = seg_closest(x, z, road[i][0], road[i][1], road[i + 1][0], road[i + 1][1])
        d = math.hypot(cx - x, cz - z)
        if d < best[0]:
            best = (d, cx, cz)
    return best


def to_world(x, z, yaw, lx, lz):
    c, s = math.cos(yaw), math.sin(yaw)
    return x + lx * c + lz * s, z - lx * s + lz * c


def pad_samples(x, z, yaw):
    pts = []
    nx, nz = int(math.ceil(PAD_W)), int(math.ceil(PAD_D))
    for i in range(nx + 1):
        for j in range(nz + 1):
            lx = -PAD_W / 2 + PAD_W * i / nx
            lz = -PAD_D / 2 + PAD_D * j / nz
            pts.append(to_world(x, z, yaw, lx, lz))
    return pts


def on_plateau(terrain, ground_y, x, z):
    t = terrain.top(x, z)
    if t is None:
        return False, None
    h, _col, flat, _ = t
    return (flat >= FLAT_NY and ground_y - 0.01 <= h <= ground_y + PLATEAU_TOL), h


def scenery_blocked(scenery, x, z):
    for _hit in scenery.hits(x, z):
        return True
    return False


def place(isl, mesh, check_scenery=True):
    terrain = TriGrid(mesh.get("terrain", []))
    scenery = TriGrid(mesh.get("scenery", []))
    ground_y = float(isl["ground_y"])
    road = isl["road"]
    hosp = [(lv["hospital"]["x"], lv["hospital"]["z"]) for lv in isl["levels"]]
    sx, sz = isl["start"]["x"], isl["start"]["z"]

    cands = []
    gx0, gx1 = math.ceil(terrain.minx / STEP), math.floor(terrain.maxx / STEP)
    gz0, gz1 = math.ceil(terrain.minz / STEP), math.floor(terrain.maxz / STEP)
    for i in range(gx0, gx1 + 1):
        x = i * STEP
        for j in range(gz0, gz1 + 1):
            z = j * STEP
            cands.append(((x - sx) ** 2 + (z - sz) ** 2, x, z))
    cands.sort()

    stats = {"hospital": 0, "road": 0, "plateau": 0, "pad_plateau": 0, "scenery": 0}
    for _d2, x, z in cands:
        dh = min(math.hypot(x - hx, z - hz) for hx, hz in hosp)
        if dh < MIN_HOSPITAL:
            stats["hospital"] += 1
            continue
        dr, rx, rz = road_nearest(road, x, z)
        if dr < MIN_ROAD:
            stats["road"] += 1
            continue
        ok, h = on_plateau(terrain, ground_y, x, z)
        if not ok:
            stats["plateau"] += 1
            continue
        yaw = math.atan2(rx - x, rz - z)
        pts = pad_samples(x, z, yaw)
        if not all(on_plateau(terrain, ground_y, px, pz)[0] for px, pz in pts):
            stats["pad_plateau"] += 1
            continue
        if check_scenery and any(scenery_blocked(scenery, px, pz) for px, pz in pts):
            stats["scenery"] += 1
            continue
        hpad = max(on_plateau(terrain, ground_y, px, pz)[1] for px, pz in pts)
        return {"x": x, "z": z, "yaw": yaw, "h": h, "hpad": hpad, "dh": dh, "dr": dr, "road_pt": (rx, rz),
                "dstart": math.sqrt(_d2), "rejected": stats}
    return None


def main():
    dry = "--dry-run" in sys.argv[1:]
    books = json.load(open(os.path.join(ROOT, "assets/world/books.json"), encoding="utf-8"))
    ok = True
    for b in books["books"]:
        jpath = os.path.join(ROOT, b["island"])
        mpath = os.path.join(ROOT, b["mesh"][:-len(".mgmb")] + ".mgmesh")
        src = open(jpath, encoding="utf-8").read()
        isl = json.loads(src)
        mesh = load_mesh(mpath)
        res = place(isl, mesh)
        lit = place(isl, mesh, check_scenery=False)
        print(f"{isl['id']}:")
        if lit is not None:
            print(f"  literal rule (no scenery check): x {lit['x']} z {lit['z']} "
                  f"yaw_deg {math.degrees(lit['yaw']):.2f}, hospital {lit['dh']:.2f}, road {lit['dr']:.2f}")
        if res is None:
            print("  no spot satisfies the rule")
            ok = False
            continue
        x, z, yaw = res["x"], res["z"], res["yaw"]
        yaw_deg = math.degrees(yaw)
        print(f"  library x {x} z {z} yaw_deg {yaw_deg:.2f}  ({res['dstart']:.2f} from start "
              f"{isl['start']['x']}, {isl['start']['z']})")
        print(f"  nearest hospital {res['dh']:.2f}  nearest road {res['dr']:.2f} at "
              f"({res['road_pt'][0]:.2f}, {res['road_pt'][1]:.2f})  plateau height {res['h']:.2f} (pad top surface max {res['hpad']:.2f})")
        print("  closer grid points rejected by: " +
              ", ".join(f"{k} {v}" for k, v in res["rejected"].items()))

        removed = []
        props = isl.get("props", {})
        for name, inst in props.items():
            keep = []
            for it in inst:
                if math.hypot(it[0] - x, it[2] - z) < PROP_CLEAR:
                    removed.append((name, it))
                else:
                    keep.append(it)
            props[name] = keep
        print(f"  props removed within {PROP_CLEAR:g}: {len(removed)}")
        for name, it in removed:
            print(f"    {name} {it}  (d {math.hypot(it[0] - x, it[2] - z):.2f})")

        corners = [to_world(x, z, yaw, lx, lz) for lx, lz in
                   ((-PAD_W / 2, PAD_D / 2), (-PAD_W / 2, -PAD_D / 2),
                    (PAD_W / 2, -PAD_D / 2), (PAD_W / 2, PAD_D / 2))]
        fx, fz = to_world(x, z, yaw, 0.0, PAD_D / 2)
        rx, rz = res["road_pt"]
        print(f"  path from ({fx:.2f}, {fz:.2f}) to ({rx:.2f}, {rz:.2f}), length {math.hypot(rx - fx, rz - fz):.2f}")
        lib = {"x": r2(x), "z": r2(z), "yaw_deg": r2(yaw_deg)}
        pad = {"x": r2(x), "z": r2(z), "yaw_deg": r2(yaw_deg), "w": r2(PAD_W), "d": r2(PAD_D),
               "y": r2(res["hpad"]), "corners": [[r2(cx), r2(cz)] for cx, cz in corners]}
        path = {"from": [r2(fx), r2(fz)], "to": [r2(rx), r2(rz)], "width": r2(PATH_W), "y": r2(res["hpad"])}

        out = {}
        for k, v in isl.items():
            if k in ("library", "library_pad", "library_path"):
                continue
            out[k] = v
            if k == "levels":
                out["library"], out["library_pad"], out["library_path"] = lib, pad, path
        text = json.dumps(out, separators=(",", ":"), ensure_ascii=False)
        if dry:
            print("  (dry run, not written)")
        elif text != src:
            with open(jpath + ".tmp", "w", encoding="utf-8") as f:
                f.write(text)
            os.replace(jpath + ".tmp", jpath)
            print(f"  wrote {b['island']}")
        else:
            print(f"  {b['island']} unchanged")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
