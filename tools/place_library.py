#!/usr/bin/env python3
"""place_library: choose the library spot on every island of the world map (LIB-1 section 6).

usage: python3 -I tools/place_library.py [--dry-run | --check]

--check measures every number of the rule again on the committed JSON (PASS / FAIL per point), then
runs the rule and fails if it would write anything different.

Rule (product owner, LIB-1): "The library stands on the island at lib_scale (0.35 map units
per metre). Add library {x, z, yaw_deg} to each island JSON by rule and commit it: the point
nearest start whose pad is >= 30 units from every hospital plot centre, >= 8 from the road, on
the grass plateau, front facing the road; remove props under the pad; add a 3.4-wide path to
the road." Re-done for the square hall: a 2 x 2 hall of 16 x 16 m rooms = 32 x 32 m in
library space, x -24 .. 8, z -21.5 .. 10.5 around the library's origin, open / front side at
local +z (the camera side). At lib_scale 0.35 that is 11.2 x 11.2 units, centred at local
(BLD_X, BLD_Z) * 0.35 = (-2.8, -1.925) units. The pad is a PAD_W x PAD_D = 15 x 15 square
centred on the building (1.9 units of pad round it on every side): pad centre = library point
+ local (PAD_OFF_X, PAD_OFF_Z). The path leaves the middle of the pad's front edge.

For each book in assets/world/books.json, reads <book>.island.json (mg-island 2) and the
island's terrain mesh (<book>_island.mgmesh, groups "terrain", "scenery" and the prop groups).
Candidates are pad centres C on a 0.5-unit grid over the terrain bounds. For each,
    yaw  = facing the nearest road point R from C (yaw_deg 0 faces +z, 90 faces +x, i.e.
           front = (sin yaw, cos yaw)),
    library point L = C - local (PAD_OFF_X, PAD_OFF_Z),
and the spot is the valid candidate whose L is nearest the island's `start` (ties: smaller
x, then smaller z of C). Valid means
  - the pad rectangle (15 x 15, oriented with the library) is >= 30 units from every hospital
    plot centre (levels[].hospital {x, z}) and >= 8 units from the road polyline (`road`,
    the centre line), measured from the rectangle, not its centre;
  - on the grass plateau: at every pad sample (0.5-unit grid + corners) the highest terrain
    surface is a flat triangle at the plateau level (ground_y .. ground_y + 0.06: the 2.2
    grass cap and the 2.25 meadow patches on it); beach (1.15 / 1.2), slopes and water fail,
    and so does anything standing on the terrain (lighthouse, boats);
  - the pad sits on bare grass (owner's decision, kept from the first placement): no triangle
    of the "scenery" group (road, ambulance start pad, ponds, fields, mounds, hedges, the
    hospital plots, the ambulance station) overlaps the pad rectangle in plan view (exact
    triangle / rectangle test; SCENERY_MARGIN = 0 grows the rectangle for it: 1.0 would
    push the old 26 x 8 Lawrance pad from 43 to 101 units from start, so it stays off);
  - the 3.4-wide path from the middle of the pad's front edge to R crosses no water (terrain
    colours 0 / 1, scenery colour 21 ponds, or a surface below the plateau's foot at 1.0)
    no hospital plot (plot_size square around each hospital, turned by its yaw) and nothing
    rising above the grass (the highest terrain or scenery surface at every sample is at most
    ground_y + 0.08: grass, meadow patches, road, the start pad (2.27); not the ambulance
    station next to the start pad, hedges or the 2.47 mounds the strip would sink into) and
    no scenery but what lies under the road point R itself (the start pad, the road): the
    path does not clip fields, mounds or hedges (exact triangle / rectangle test); the rest
    sampled on a 0.25-unit grid.
The point the rule gives without the bare-grass check is printed too.

Props: every prop instance (props.<group>[] = [x, y, z, scale, yaw]) whose footprint (the
group's mesh radius in x/z times scale) comes within PROP_MARGIN (1.0) of the pad rectangle
or within PATH_MARGIN (0.5) of the path strip is removed, so nothing stands on the pad, leans
over its edge or blocks the path.

Writes, after "levels":

    "library":      {"x", "z", "yaw_deg"}  (the building's origin, as the game places it)
    "library_pad":  {"x", "z", "yaw_deg", "w": 15, "d": 15, "y", "corners": [[x, z] x 4]}
                    x, z = the pad centre (library + local (-2.8, -1.925)); w along the library's
                    local x, d along its local z (the front edge is local z = +d/2 from the
                    pad centre); y = highest plateau surface under it; corners in world x/z,
                    in pad-local terms (-w/2, +d/2) (-w/2, -d/2) (+w/2, -d/2) (+w/2, +d/2);
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
LIB_SCALE = 0.35     # map units per library metre
BLD_X = -8.0         # building centre in library metres: x -24 .. 8
BLD_Z = -5.5         #                                     z -21.5 .. 10.5
PAD_OFF_X = BLD_X * LIB_SCALE
PAD_OFF_Z = BLD_Z * LIB_SCALE
PAD_W = 15.0         # the 11.2 x 11.2 building + ~3.8
PAD_D = 15.0
PAD_SAMPLE = 0.5
SCENERY_MARGIN = 0.0  # extra bare grass around the pad (off, see the docstring)
PATH_W = 3.4
PATH_SAMPLE = 0.25
PROP_MARGIN = 1.0    # footprint clearance to the pad edge
PATH_MARGIN = 0.5    # footprint clearance to the path edge
PLATEAU_TOL = 0.06   # accepted plateau surfaces: ground_y - 0.01 .. ground_y + PLATEAU_TOL
FLAT_NY = 0.995      # a flat triangle's normal y
WATER_T = {0, 1}     # terrain colours: sea, shallows
WATER_S = {21}       # scenery colour: ponds
LOW_Y = 1.0          # anything below is under the plateau's foot (sea level)
PATH_RISE = 0.08     # the path's ground: nothing higher than ground_y + PATH_RISE
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


def rect(cx, cz, yaw, w, d):
    """Corners (counter-clockwise in local terms) of a w x d rectangle centred on (cx, cz)."""
    return [to_world(cx, cz, yaw, sx * w / 2, sz * d / 2) for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1))]


def poly_dist(poly, px, pz):
    """0 inside the convex polygon, else the distance to its boundary."""
    inside, sgn, best = True, None, 1e30
    n = len(poly)
    for i in range(n):
        ax, az = poly[i]
        bx, bz = poly[(i + 1) % n]
        cr = (bx - ax) * (pz - az) - (bz - az) * (px - ax)
        if abs(cr) > 1e-9:
            if sgn is None:
                sgn = cr > 0
            elif (cr > 0) != sgn:
                inside = False
        cx, cz = seg_closest(px, pz, ax, az, bx, bz)
        best = min(best, math.hypot(cx - px, cz - pz))
    return 0.0 if inside else best


def seg_seg(a, b, c, d):
    def cross(o, p, q):
        return (p[0] - o[0]) * (q[1] - o[1]) - (p[1] - o[1]) * (q[0] - o[0])
    if (cross(a, b, c) > 0) != (cross(a, b, d) > 0) and (cross(c, d, a) > 0) != (cross(c, d, b) > 0):
        return 0.0
    best = 1e30
    for p, q, r in ((a, c, d), (b, c, d), (c, a, b), (d, a, b)):
        x, z = seg_closest(p[0], p[1], q[0], q[1], r[0], r[1])
        best = min(best, math.hypot(x - p[0], z - p[1]))
    return best


def polyline_poly_dist(line, poly):
    best = min(poly_dist(poly, x, z) for x, z in line)
    if best == 0.0:
        return 0.0
    n = len(poly)
    for i in range(len(line) - 1):
        for j in range(n):
            best = min(best, seg_seg(line[i], line[i + 1], poly[j], poly[(j + 1) % n]))
    return best


def rect_samples(cx, cz, yaw, w, d, step):
    nx, nz = max(1, int(math.ceil(w / step))), max(1, int(math.ceil(d / step)))
    return [to_world(cx, cz, yaw, -w / 2 + w * i / nx, -d / 2 + d * j / nz)
            for i in range(nx + 1) for j in range(nz + 1)]


def on_plateau(terrain, ground_y, x, z):
    t = terrain.top(x, z)
    if t is None:
        return False, None
    h, _col, flat, _ = t
    return (flat >= FLAT_NY and ground_y - 0.01 <= h <= ground_y + PLATEAU_TOL), h


def convex_overlap(a, b):
    """Separating-axis test for two convex polygons in x/z (touching counts as overlap)."""
    for poly in (a, b):
        n = len(poly)
        for i in range(n):
            ex, ez = poly[(i + 1) % n][0] - poly[i][0], poly[(i + 1) % n][1] - poly[i][1]
            ax, az = -ez, ex
            pa = [ax * x + az * z for x, z in a]
            pb = [ax * x + az * z for x, z in b]
            if max(pa) < min(pb) - 1e-9 or max(pb) < min(pa) - 1e-9:
                return False
    return True


def scenery_overlaps(scenery, poly, allowed=()):
    xs, zs = [p[0] for p in poly], [p[1] for p in poly]
    seen = set()
    for cx in range(math.floor(min(xs) / CELL), math.floor(max(xs) / CELL) + 1):
        for cz in range(math.floor(min(zs) / CELL), math.floor(max(zs) / CELL) + 1):
            for idx in scenery.cells.get((cx, cz), ()):
                if idx in seen:
                    continue
                seen.add(idx)
                if scenery.tris[idx][2] in allowed:
                    continue
                v = scenery.tris[idx][0]
                if convex_overlap(poly, [(v[0], v[2]), (v[3], v[5]), (v[6], v[8])]):
                    return True
    return False


def wet(terrain, scenery, x, z):
    hs = [(h[0], h[1], "t") for h in terrain.hits(x, z)] + [(h[0], h[1], "s") for h in scenery.hits(x, z)]
    if not hs:
        return True
    top = max(h[0] for h in hs)
    if top < LOW_Y:
        return True
    return any((tag == "t" and col in WATER_T) or (tag == "s" and col in WATER_S)
               for h, col, tag in hs if h >= top - 0.3)


def raised(terrain, scenery, limit, x, z):
    for g in (terrain, scenery):
        for h in g.hits(x, z):
            if h[0] > limit:
                return True
    return False


def path_rect(fx, fz, tx, tz):
    L = math.hypot(tx - fx, tz - fz)
    yaw = math.atan2(tx - fx, tz - fz)
    return (fx + tx) / 2, (fz + tz) / 2, yaw, L


def plot_rects(isl):
    ps = isl["plot_size"]
    return [rect(lv["hospital"]["x"], lv["hospital"]["z"], lv["hospital"]["yaw"], ps, ps) for lv in isl["levels"]]


def place(isl, mesh, off_x, check_scenery=True):
    terrain = TriGrid(mesh.get("terrain", []))
    scenery = TriGrid(mesh.get("scenery", []))
    ground_y = float(isl["ground_y"])
    road = isl["road"]
    hosp = [(lv["hospital"]["x"], lv["hospital"]["z"]) for lv in isl["levels"]]
    plots = plot_rects(isl)
    sx, sz = isl["start"]["x"], isl["start"]["z"]
    off = math.hypot(off_x, PAD_OFF_Z)

    cands = []
    gx0, gx1 = math.ceil(terrain.minx / STEP), math.floor(terrain.maxx / STEP)
    gz0, gz1 = math.ceil(terrain.minz / STEP), math.floor(terrain.maxz / STEP)
    for i in range(gx0, gx1 + 1):
        x = i * STEP
        for j in range(gz0, gz1 + 1):
            z = j * STEP
            cands.append((math.hypot(x - sx, z - sz), x, z))
    cands.sort()

    stats = {"hospital": 0, "road": 0, "plateau": 0, "pad_plateau": 0, "scenery": 0, "path": 0}
    best = None
    for dc, x, z in cands:
        if best is not None and dc > best["dstart"] + off + 1e-9:
            break  # |L - C| is fixed, so no later C can give a nearer library point
        if not on_plateau(terrain, ground_y, x, z)[0]:
            stats["plateau"] += 1
            continue
        dr, rx, rz = road_nearest(road, x, z)
        yaw = math.atan2(rx - x, rz - z)
        pad = rect(x, z, yaw, PAD_W, PAD_D)
        dh = min(poly_dist(pad, hx, hz) for hx, hz in hosp)
        if dh < MIN_HOSPITAL:
            stats["hospital"] += 1
            continue
        dpr = polyline_poly_dist(road, pad)
        if dpr < MIN_ROAD:
            stats["road"] += 1
            continue
        pts = rect_samples(x, z, yaw, PAD_W, PAD_D, PAD_SAMPLE)
        tops = [on_plateau(terrain, ground_y, px, pz) for px, pz in pts]
        if not all(t[0] for t in tops):
            stats["pad_plateau"] += 1
            continue
        if check_scenery and scenery_overlaps(scenery, rect(x, z, yaw, PAD_W + 2 * SCENERY_MARGIN,
                                                            PAD_D + 2 * SCENERY_MARGIN)):
            stats["scenery"] += 1
            continue
        fx, fz = to_world(x, z, yaw, 0.0, PAD_D / 2)
        pcx, pcz, pyaw, plen = path_rect(fx, fz, rx, rz)
        psamp = rect_samples(pcx, pcz, pyaw, PATH_W, plen, PATH_SAMPLE)
        allowed = {h[1] for h in scenery.hits(rx, rz)}
        if scenery_overlaps(scenery, rect(pcx, pcz, pyaw, PATH_W, plen), allowed) or \
                any(wet(terrain, scenery, px, pz) or raised(terrain, scenery, ground_y + PATH_RISE, px, pz)
                    for px, pz in psamp) or \
                any(poly_dist(pl, px, pz) == 0.0 for pl in plots for px, pz in psamp):
            stats["path"] += 1
            continue
        lx, lz = to_world(x, z, yaw, -off_x, -PAD_OFF_Z)
        dl = math.hypot(lx - sx, lz - sz)
        if best is None or dl < best["dstart"] - 1e-9:
            best = {"x": x, "z": z, "lx": lx, "lz": lz, "yaw": yaw, "hpad": max(t[1] for t in tops),
                    "dh": dh, "dr": dpr, "road_pt": (rx, rz), "front": (fx, fz), "pad": pad,
                    "dstart": dl, "rejected": dict(stats)}
    return best


def check_committed(isl, mesh, radius):
    """--check: every number of the rule measured again on the committed library / library_pad / library_path
    (not by trusting the search). Returns a list of (name, ok, detail)."""
    out = []
    lib, pad, path = isl.get("library"), isl.get("library_pad"), isl.get("library_path")
    if not (lib and pad and path):
        return [("library, library_pad, library_path present", False, "missing")]
    scene = json.load(open(os.path.join(ROOT, "assets/menu/library.scene.json"), encoding="utf-8"))
    hall, s = scene["hall"], scene["lib_scale"]
    bw, bd = (hall["x_max"] - hall["x_min"]) * s, (hall["z_max"] - hall["z_min"]) * s
    bcx, bcz = 0.5 * (hall["x_min"] + hall["x_max"]) * s, 0.5 * (hall["z_min"] + hall["z_max"]) * s
    out.append(("lib_scale 0.35, building 11.2 x 11.2 centred at local (PAD_OFF_X, PAD_OFF_Z)",
                abs(s - LIB_SCALE) < 1e-9 and abs(bw - 11.2) < 1e-6 and abs(bd - 11.2) < 1e-6 and
                abs(bcx - PAD_OFF_X) < 1e-6 and abs(bcz - PAD_OFF_Z) < 1e-6,
                f"scale {s}, {bw:.2f} x {bd:.2f}, centre {bcx:.3f}, {bcz:.3f}"))
    yaw = math.radians(lib["yaw_deg"])
    cx, cz = to_world(lib["x"], lib["z"], yaw, PAD_OFF_X, PAD_OFF_Z)
    out.append(("pad 15 x 15 centred on the building, same yaw",
                pad["w"] == PAD_W and pad["d"] == PAD_D and abs(pad["yaw_deg"] - lib["yaw_deg"]) < 1e-6 and
                math.hypot(cx - pad["x"], cz - pad["z"]) < 0.02 and pad["w"] >= bw + 2 * 1.9 - 1e-6,
                f"{pad['w']} x {pad['d']}, centre off by {math.hypot(cx - pad['x'], cz - pad['z']):.3f}"))
    x, z = pad["x"], pad["z"]
    prect = rect(x, z, yaw, PAD_W, PAD_D)
    dh = min(poly_dist(prect, lv["hospital"]["x"], lv["hospital"]["z"]) for lv in isl["levels"])
    out.append((f"pad >= {MIN_HOSPITAL:g} from every hospital plot centre", dh >= MIN_HOSPITAL - 0.02, f"nearest {dh:.2f}"))
    dr = polyline_poly_dist(isl["road"], prect)
    out.append((f"pad >= {MIN_ROAD:g} from the road", dr >= MIN_ROAD - 0.02, f"{dr:.2f}"))
    terrain, scenery = TriGrid(mesh.get("terrain", [])), TriGrid(mesh.get("scenery", []))
    gy = float(isl["ground_y"])
    tops = [on_plateau(terrain, gy, px, pz) for px, pz in rect_samples(x, z, yaw, PAD_W, PAD_D, PAD_SAMPLE)]
    out.append(("pad on the grass plateau (every 0.5 sample flat at ground_y)", all(t[0] for t in tops),
                f"{sum(1 for t in tops if not t[0])} of {len(tops)} samples off"))
    out.append(("pad on bare grass (no scenery triangle under it)", not scenery_overlaps(scenery, prect), ""))
    _d, rx, rz = road_nearest(isl["road"], x, z)
    face = math.degrees(math.atan2(rx - x, rz - z)) - lib["yaw_deg"]
    face = (face + 180.0) % 360.0 - 180.0
    out.append(("front faces the road (front = local +z towards the nearest road point)", abs(face) < 0.05, f"off by {face:.3f} deg"))
    fx, fz = to_world(x, z, yaw, 0.0, PAD_D / 2)
    ends = math.hypot(path["from"][0] - fx, path["from"][1] - fz) + math.hypot(path["to"][0] - rx, path["to"][1] - rz)
    out.append(("path 3.4 wide from the pad's front edge to the nearest road point", path["width"] == PATH_W and ends < 0.03,
                f"ends off by {ends:.3f}"))
    pcx, pcz, pyaw, plen = path_rect(path["from"][0], path["from"][1], path["to"][0], path["to"][1])
    psamp = rect_samples(pcx, pcz, pyaw, PATH_W, plen, PATH_SAMPLE)
    allowed = {h[1] for h in scenery.hits(path["to"][0], path["to"][1])}
    bad = scenery_overlaps(scenery, rect(pcx, pcz, pyaw, PATH_W, plen), allowed) or \
        any(wet(terrain, scenery, px, pz) or raised(terrain, scenery, gy + PATH_RISE, px, pz) for px, pz in psamp) or \
        any(poly_dist(pl, px, pz) == 0.0 for pl in plot_rects(isl) for px, pz in psamp)
    out.append(("path dry, on the grass, through no plot or scenery", not bad, f"length {plen:.2f}"))
    path_poly = rect(pcx, pcz, pyaw, PATH_W, plen)
    near = [(n, it) for n, inst in isl.get("props", {}).items() for it in inst
            if poly_dist(prect, it[0], it[2]) - radius[n] * it[3] < PROP_MARGIN or
            poly_dist(path_poly, it[0], it[2]) - radius[n] * it[3] < PATH_MARGIN]
    out.append(("no prop on or over the pad or the path", not near, f"{len(near)} left"))
    return out


def main():
    check = "--check" in sys.argv[1:]
    dry = "--dry-run" in sys.argv[1:] or check
    failed = []
    off_x = PAD_OFF_X
    books = json.load(open(os.path.join(ROOT, "assets/world/books.json"), encoding="utf-8"))
    ok = True
    for b in books["books"]:
        jpath = os.path.join(ROOT, b["island"])
        mpath = os.path.join(ROOT, b["mesh"][:-len(".mgmb")] + ".mgmesh")
        src = open(jpath, encoding="utf-8").read()
        isl = json.loads(src)
        mesh = load_mesh(mpath)
        if check:
            rad = {}
            for g in isl.get("props", {}):
                rad[g] = max([math.hypot(v[i], v[i + 2]) for _c, v in mesh.get(g, []) for i in (0, 3, 6)] or [0.0])
            for name, good, detail in check_committed(isl, mesh, rad):
                print(f"  {'PASS' if good else 'FAIL'}  {isl['id']}: {name}" + (f" ({detail})" if detail else ""))
                if not good:
                    failed.append(f"{isl['id']}: {name}")
            isl = json.loads(src)
        res = place(isl, mesh, off_x)
        lit = place(isl, mesh, off_x, check_scenery=False)
        print(f"{isl['id']}:  (pad offset from the library point: local x {off_x:g}, z {PAD_OFF_Z:g})")
        if lit is not None:
            print(f"  without the bare-grass check: library x {lit['lx']:.2f} z {lit['lz']:.2f} "
                  f"yaw_deg {math.degrees(lit['yaw']):.2f}, pad-hospital {lit['dh']:.2f}, pad-road {lit['dr']:.2f}")
        if res is None:
            print("  no spot satisfies the rule")
            ok = False
            continue
        x, z, yaw = res["x"], res["z"], res["yaw"]
        lx, lz = res["lx"], res["lz"]
        yaw_deg = math.degrees(yaw)
        print(f"  library x {lx:.2f} z {lz:.2f} yaw_deg {yaw_deg:.2f}  ({res['dstart']:.2f} from start "
              f"{isl['start']['x']}, {isl['start']['z']}); pad centre {x}, {z}")
        print(f"  pad to nearest hospital centre {res['dh']:.2f}, to the road {res['dr']:.2f}; nearest road point "
              f"({res['road_pt'][0]:.2f}, {res['road_pt'][1]:.2f}); pad top surface max {res['hpad']:.2f}")
        print("  candidates rejected before it: " + ", ".join(f"{k} {v}" for k, v in res["rejected"].items()))

        # footprint radii of the prop groups (mesh x/z extent; instances scale it)
        radius = {}
        for g in isl.get("props", {}):
            r = 0.0
            for _c, v in mesh.get(g, []):
                for i in (0, 3, 6):
                    r = max(r, math.hypot(v[i], v[i + 2]))
            radius[g] = r
        fx, fz = res["front"]
        rx, rz = res["road_pt"]
        pcx, pcz, pyaw, plen = path_rect(fx, fz, rx, rz)
        prect = rect(pcx, pcz, pyaw, PATH_W, plen)
        removed = []
        props = isl.get("props", {})
        for name, inst in props.items():
            keep = []
            for it in inst:
                fr = radius[name] * it[3]
                dp = poly_dist(res["pad"], it[0], it[2]) - fr
                dq = poly_dist(prect, it[0], it[2]) - fr
                if dp < PROP_MARGIN or dq < PATH_MARGIN:
                    removed.append((name, it, dp, dq))
                else:
                    keep.append(it)
            props[name] = keep
        print(f"  props removed (footprint within {PROP_MARGIN:g} of the pad or {PATH_MARGIN:g} of the path): {len(removed)}")
        for name, it, dp, dq in removed:
            print(f"    {name} {it}  (clearance pad {dp:.2f}, path {dq:.2f})")

        corners = [to_world(x, z, yaw, cx, cz) for cx, cz in
                   ((-PAD_W / 2, PAD_D / 2), (-PAD_W / 2, -PAD_D / 2),
                    (PAD_W / 2, -PAD_D / 2), (PAD_W / 2, PAD_D / 2))]
        print(f"  path from ({fx:.2f}, {fz:.2f}) to ({rx:.2f}, {rz:.2f}), length {plen:.2f}")
        lib = {"x": r2(lx), "z": r2(lz), "yaw_deg": r2(yaw_deg)}
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
        if check:
            same = text == src
            print(f"  {'PASS' if same else 'FAIL'}  {isl['id']}: the rule run again gives the committed spot, path and props")
            if not same:
                failed.append(f"{isl['id']}: the rule gives another result than the committed JSON")
        elif dry:
            print("  (dry run, not written)")
        elif text != src:
            with open(jpath + ".tmp", "w", encoding="utf-8") as f:
                f.write(text)
            os.replace(jpath + ".tmp", jpath)
            print(f"  wrote {b['island']}")
        else:
            print(f"  {b['island']} unchanged")
    if check:
        print("place_library --check: " + ("PASS" if ok and not failed else "FAIL: " + "; ".join(failed)))
    return 0 if ok and not failed else 1


if __name__ == "__main__":
    sys.exit(main())
