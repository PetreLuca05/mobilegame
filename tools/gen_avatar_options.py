#!/usr/bin/env python3
"""gen_avatar_options: character-creator option meshes for the faceted avatar.

Reads assets/avatar/avatar.mgmesh (avatar_body, avatar_head, avatar_hair) and
writes assets/avatar/avatar_options.mgmesh (mgmesh text, see tools/meshgen.py).
Python 3 stdlib only, deterministic (no randomness, fixed float formatting).

Spaces (y up, forward +z, the avatar's left is +x, metres):
  head space  origin = head centre (head: faceted sphere r 0.25, eyes at (+-0.095, 0, 0.225))
  body space  origin = body base (avatar_body, a lathe from y 0 to 0.64)
  root space  origin on the ground (npc_cane: the cane's foot; npc_bucket: its bottom centre)

Groups:
  hair_bob hair_bun hair_spiky hair_curly       head space, <= 120 tris
  body_suit       avatar_body (zone 0) + front detail patches by zone:
                  1 V-neck, 2 coat front (d8dce2), 3 coat buttons (c9ced6),
                  4 cross, 5 pocket (panel ffffff, flap b3b3b8), 6 badge,
                  7 stethoscope (tube 2e3440, chest piece c8cdd3)
  body_suit_hull  avatar_body only (outline hull)
  acc_sunglasses acc_cat_ears acc_mask npc_hat  head space
  npc_cane npc_bucket                           root space

Everything that rests on the head or body is built against the exported
facets, not an ideal sphere / lathe: body patches are the body's own front
facets clipped to each shape and pushed 4 mm along +z (watertight over the
creases), the mask and its loops are the head's own facets clipped and scaled
out about the head centre, and every part is checked by sampling its triangles
against the facet planes (the head and the body are convex).

usage: gen_avatar_options.py [--out PATH] [--preview PNG] [--no-check]
"""
import math
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets", "avatar", "avatar.mgmesh")
DST = os.path.join(ROOT, "assets", "avatar", "avatar_options.mgmesh")

HEAD_R = 0.25
HEAD_Y = 0.86  # head centre above the body base (avatar.json)
EYE = (0.095, 0.0, 0.225)

# palette: index -> (rgb, zone)
PALETTE = [
    (0, "ffffff", 0),   # plain, tinted per instance
    (1, "ffffff", 1),   # V-neck
    (2, "d8dce2", 2),   # coat front: lapels + opening line
    (3, "c9ced6", 3),   # coat buttons
    (4, "ffffff", 4),   # cross
    (5, "ffffff", 5),   # pocket panel
    (6, "b3b3b8", 5),   # pocket flap
    (7, "ffffff", 6),   # badge
    (8, "2e3440", 7),   # stethoscope tube
    (9, "c8cdd3", 7),   # stethoscope chest piece
    (10, "e6e6e6", 0),  # mask pleat shade
]
C_PLAIN, C_VNECK, C_COAT, C_BUTTON, C_CROSS, C_POCKET, C_FLAP, C_BADGE, C_TUBE, C_STEEL, C_PLEAT = range(11)


# ---------------------------------------------------------------- vectors
def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def mul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def length(a):
    return math.sqrt(dot(a, a))


def norm(a):
    n = length(a)
    return (a[0] / n, a[1] / n, a[2] / n)


def lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


def tri_normal(a, b, c):
    return cross(sub(b, a), sub(c, a))


def rot_x(p, ang):
    c, s = math.cos(ang), math.sin(ang)
    return (p[0], p[1] * c - p[2] * s, p[1] * s + p[2] * c)


def rot_y(p, ang):
    c, s = math.cos(ang), math.sin(ang)
    return (p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c)


def rot_z(p, ang):
    c, s = math.cos(ang), math.sin(ang)
    return (p[0] * c - p[1] * s, p[0] * s + p[1] * c, p[2])


def basis(n):
    """Two unit vectors perpendicular to unit n (u roughly horizontal)."""
    up = (0.0, 1.0, 0.0) if abs(n[1]) < 0.9 else (1.0, 0.0, 0.0)
    u = norm(cross(up, n))
    v = cross(n, u)
    return u, v


# ---------------------------------------------------------------- mesh helpers
class Mesh:
    def __init__(self):
        self.tris = []  # (colour, a, b, c)
        self.ncap = None  # hair: how many leading triangles are the cap

    def tri(self, c, a, b, d, inside=None, outside=None):
        """Add a triangle; with `inside` (a point behind the surface) or
        `outside` the winding is fixed so the normal points away from / to it."""
        if inside is not None or outside is not None:
            n = tri_normal(a, b, d)
            cen = mul(add(add(a, b), d), 1.0 / 3.0)
            ref = sub(cen, inside) if inside is not None else sub(outside, cen)
            if dot(n, ref) < 0:
                b, d = d, b
        self.tris.append((c, a, b, d))

    def poly(self, c, pts, inside=None, outside=None):
        for i in range(1, len(pts) - 1):
            self.tri(c, pts[0], pts[i], pts[i + 1], inside, outside)

    def extend(self, other):
        self.tris.extend(other.tris)


def load_mgmesh(path):
    groups, cur = {}, None
    with open(path, encoding="utf-8") as f:
        for line in f:
            p = line.split()
            if not p or p[0].startswith("#"):
                continue
            if p[0] == "g":
                cur = groups.setdefault(p[1], [])
            elif p[0] == "t":
                v = [float(x) for x in p[2:11]]
                cur.append((int(p[1]), tuple(v[0:3]), tuple(v[3:6]), tuple(v[6:9])))
    return groups


class Convex:
    """A convex closed mesh as facet planes (n unit, d): inside when n.p < d."""

    def __init__(self, tris, offset=(0.0, 0.0, 0.0)):
        self.tris = [(add(a, offset), add(b, offset), add(c, offset)) for _, a, b, c in tris]
        self.planes = []
        for a, b, c in self.tris:
            n = tri_normal(a, b, c)
            if length(n) < 1e-10:
                continue
            n = norm(n)
            self.planes.append((n, dot(n, a)))

    def dist(self, p):
        """Signed distance lower bound (exact off faces): > 0 outside."""
        return max(dot(n, p) - d for n, d in self.planes)


def ray_hits(tris, o, d):
    """Distances t > 0 where the ray o + t d crosses a triangle."""
    out = []
    for a, b, c in tris:
        e1, e2 = sub(b, a), sub(c, a)
        h = cross(d, e2)
        det = dot(e1, h)
        if abs(det) < 1e-12:
            continue
        f = 1.0 / det
        s = sub(o, a)
        u = f * dot(s, h)
        if u < -1e-9 or u > 1 + 1e-9:
            continue
        q = cross(s, e1)
        v = f * dot(d, q)
        if v < -1e-9 or u + v > 1 + 1e-9:
            continue
        t = f * dot(e2, q)
        if t > 1e-9:
            out.append(t)
    return out


def clip(poly, f):
    """Sutherland-Hodgman against the half-space f(p) >= 0 (f linear)."""
    out = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        fa, fb = f(a), f(b)
        if fa >= 0:
            out.append(a)
        if (fa >= 0) != (fb >= 0):
            out.append(lerp(a, b, fa / (fa - fb)))
    return out


def tidy(poly, eps=1e-6):
    """Drop repeated and collinear points of a convex polygon."""
    pts = []
    for p in poly:
        if not pts or length(sub(p, pts[-1])) > eps:
            pts.append(p)
    while len(pts) > 1 and length(sub(pts[0], pts[-1])) <= eps:
        pts.pop()
    changed = True
    while changed and len(pts) >= 3:
        changed = False
        for i in range(len(pts)):
            a, b, c = pts[i - 1], pts[i], pts[(i + 1) % len(pts)]
            if length(cross(sub(b, a), sub(c, b))) < 1e-9:
                pts.pop(i)
                changed = True
                break
    return pts if len(pts) >= 3 else []


def poly_area(poly):
    s = (0.0, 0.0, 0.0)
    for i in range(1, len(poly) - 1):
        s = add(s, tri_normal(poly[0], poly[i], poly[i + 1]))
    return 0.5 * length(s)


def coplanar_polys(tris):
    """Merge a convex mesh's triangles that share a plane into convex polygons
    (the body's lathe quads), each ordered CCW around its outward normal."""
    groups = []
    for _, a, b, c in tris:
        n = tri_normal(a, b, c)
        if length(n) < 1e-12:
            continue
        n = norm(n)
        d = dot(n, a)
        for g in groups:
            if dot(g[0], n) > 1 - 1e-7 and abs(g[1] - d) < 1e-6:
                g[2].extend([a, b, c])
                break
        else:
            groups.append([n, d, [a, b, c]])
    polys = []
    for n, _, pts in groups:
        uniq = []
        for p in pts:
            if all(length(sub(p, q)) > 1e-7 for q in uniq):
                uniq.append(p)
        u, v = basis(n)
        cen = mul(uniq[0], 0)
        for p in uniq:
            cen = add(cen, p)
        cen = mul(cen, 1.0 / len(uniq))
        uniq.sort(key=lambda p: math.atan2(dot(sub(p, cen), v), dot(sub(p, cen), u)))
        polys.append((n, uniq))
    return polys


# ---------------------------------------------------------------- inputs
SRC_GROUPS = load_mgmesh(SRC)
BODY_TRIS = SRC_GROUPS["avatar_body"]
HEAD_TRIS = SRC_GROUPS["avatar_head"]
HAIR_TRIS = SRC_GROUPS["avatar_hair"]
BODY = Convex(BODY_TRIS)
HEAD = Convex(HEAD_TRIS)
HEAD_IN_BODY = Convex(HEAD_TRIS, (0.0, HEAD_Y, 0.0))
BODY_FRONT = [(n, p) for n, p in coplanar_polys(BODY_TRIS) if n[2] > 0.02]
BODY_POLYS = coplanar_polys(BODY_TRIS)
HEAD_FACETS = [(a, b, c) for _, a, b, c in HEAD_TRIS]


def body_z(x, y):
    """Front surface z of the body at (x, y) (projection along z)."""
    best = None
    for n, poly in BODY_FRONT:
        inside = True
        for i in range(len(poly)):
            a, b = poly[i], poly[(i + 1) % len(poly)]
            if (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]) < -1e-9:
                inside = False
                break
        if inside:
            d = dot(n, poly[0])
            z = (d - n[0] * x - n[1] * y) / n[2]
            best = z if best is None else max(best, z)
    if best is None:
        raise ValueError(f"no body front at ({x}, {y})")
    return best


def body_normal_at(p):
    """Normal of the body facet nearest (by plane distance) to p."""
    return max(BODY.planes, key=lambda pl: dot(pl[0], p) - pl[1])[0]


def body_patch(mesh, colour, shape, dz=0.0045):
    """Body front facets clipped to the convex xy polygon `shape` (CCW seen
    from +z), pushed dz along +z: shared crease points move together, so the
    patch is watertight and follows every facet."""
    area2 = sum(shape[i - 1][0] * shape[i][1] - shape[i][0] * shape[i - 1][1] for i in range(len(shape)))
    if area2 < 0:
        shape = list(reversed(shape))
    for _, poly in BODY_FRONT:
        p = list(poly)
        for i in range(len(shape)):
            ax, ay = shape[i]
            bx, by = shape[(i + 1) % len(shape)]
            p = clip(p, lambda q, ax=ax, ay=ay, bx=bx, by=by: (bx - ax) * (q[1] - ay) - (by - ay) * (q[0] - ax))
            if not p:
                break
        p = tidy(p)
        if not p or poly_area(p) < 2e-7:
            continue
        mesh.poly(colour, [(q[0], q[1], q[2] + dz) for q in p])


def head_patch(mesh, colour_of, halfspaces, scale, bands=None):
    """Head facets clipped to the half-spaces (linear f(p) >= 0), split into
    horizontal bands (list of (y_top, y_bottom, colour)), scaled about the
    head centre: a parallel copy of the facets `(scale - 1) x 0.234` m out."""
    for a, b, c in HEAD_FACETS:
        p = [a, b, c]
        for f in halfspaces:
            p = clip(p, f)
            if not p:
                break
        if not p:
            continue
        pieces = [(p, colour_of)]
        if bands:
            pieces = []
            for top, bot, col in bands:
                q = clip(clip(p, lambda r, top=top: top - r[1]), lambda r, bot=bot: r[1] - bot)
                if q:
                    pieces.append((q, col))
        for q, col in pieces:
            q = tidy(q)
            if not q or poly_area(q) < 2e-7:
                continue
            mesh.poly(col, [mul(r, scale) for r in q])


# ---------------------------------------------------------------- primitives
def cap(R, tilt_deg, extent_deg, rings, segs, phase=0.0):
    """Spherical cap about a pole tilted back (towards -z) by tilt_deg."""
    m = Mesh()
    tilt = math.radians(tilt_deg)

    def pt(i, j):
        phi = math.radians(extent_deg) * i / rings
        psi = 2 * math.pi * (j + phase) / segs
        local = (math.sin(phi) * math.sin(psi), math.cos(phi), math.sin(phi) * math.cos(psi))
        return mul(rot_x(local, -tilt), R)

    o = (0.0, 0.0, 0.0)
    pole = pt(0, 0)
    for j in range(segs):
        m.tri(C_PLAIN, pole, pt(1, j), pt(1, j + 1), inside=o)
    for i in range(1, rings):
        for j in range(segs):
            a, b, c, d = pt(i, j), pt(i, j + 1), pt(i + 1, j), pt(i + 1, j + 1)
            m.tri(C_PLAIN, a, c, b, inside=o)
            m.tri(C_PLAIN, b, c, d, inside=o)
    return m


def pyramid(colour, base_c, axis, ex, ez, height, n, spin=0.0, closed=False, apex_shift=(0.0, 0.0, 0.0)):
    """n-sided pyramid on an elliptic base (half-axes ex along the base's u,
    ez along v) around `axis` (unit)."""
    m = Mesh()
    u, v = basis(axis)
    ring = []
    for k in range(n):
        a = 2 * math.pi * (k + spin) / n
        ring.append(add(base_c, add(mul(u, ex * math.cos(a)), mul(v, ez * math.sin(a)))))
    apex = add(add(base_c, mul(axis, height)), apex_shift)
    for k in range(n):
        m.tri(colour, ring[k], ring[(k + 1) % n], apex, inside=base_c)
    if closed:
        m.poly(colour, ring, outside=sub(base_c, axis))
    return m


def icosahedron(colour, c, r, squash=(1.0, 1.0, 1.0), turn=0.0):
    t = (1 + math.sqrt(5)) / 2
    vs = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
          (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    fs = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
          (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10),
          (8, 6, 7), (9, 8, 1)]
    pts = []
    for x, y, z in vs:
        p = rot_y(norm((x, y, z)), turn)
        pts.append(add(c, (p[0] * r * squash[0], p[1] * r * squash[1], p[2] * r * squash[2])))
    m = Mesh()
    for a, b, d in fs:
        m.tri(colour, pts[a], pts[b], pts[d], inside=c)
    return m


def tube(colour, pts, r, sides, closed=False, caps=(False, False), spin=0.0):
    """Tube along a polyline, mitred joints (rings sized so the wall keeps its
    thickness), parallel-transported frames."""
    m = Mesh()
    n = len(pts)
    tangents = []
    for i in range(n):
        if closed:
            t_in = norm(sub(pts[i], pts[i - 1]))
            t_out = norm(sub(pts[(i + 1) % n], pts[i]))
        else:
            t_in = norm(sub(pts[i], pts[i - 1])) if i > 0 else None
            t_out = norm(sub(pts[i + 1], pts[i])) if i < n - 1 else None
        if t_in is None:
            tangents.append((t_out, 1.0))
        elif t_out is None:
            tangents.append((t_in, 1.0))
        else:
            t = norm(add(t_in, t_out))
            tangents.append((t, 1.0 / max(dot(t, t_out), 0.35)))
    u, _ = basis(tangents[0][0])
    rings = []
    for i in range(n):
        t, k = tangents[i]
        u = norm(sub(u, mul(t, dot(u, t))))  # transport
        v = cross(t, u)
        ring = []
        for s in range(sides):
            a = 2 * math.pi * (s + spin) / sides
            off = add(mul(u, math.cos(a) * r), mul(v, math.sin(a) * r))
            ring.append(add(pts[i], off if k == 1.0 else miter(off, t, pts, i, n, closed, k)))
        rings.append(ring)
    segs = n if closed else n - 1
    for i in range(segs):
        ra, rb = rings[i], rings[(i + 1) % n]
        ca, cb = pts[i], pts[(i + 1) % n]
        mid = lerp(ca, cb, 0.5)
        for s in range(sides):
            a, b = ra[s], ra[(s + 1) % sides]
            c, d = rb[s], rb[(s + 1) % sides]
            m.tri(colour, a, b, c, inside=mid)
            m.tri(colour, b, d, c, inside=mid)
    if not closed:
        if caps[0]:
            m.poly(colour, rings[0], inside=pts[1])
        if caps[1]:
            m.poly(colour, rings[-1], inside=pts[-2])
    return m


def miter(off, t, pts, i, n, closed, k):
    """Push a ring offset onto the miter plane: stretch the component along
    the bend (in the plane of the two segments) by k."""
    a = pts[i - 1] if (closed or i > 0) else pts[i]
    b = pts[(i + 1) % n]
    bend = sub(sub(b, pts[i]), mul(t, dot(sub(b, pts[i]), t)))
    if length(bend) < 1e-9:
        return off
    bend = norm(bend)
    c = dot(off, bend)
    return add(off, mul(bend, c * (k - 1.0)))


def prism(colour, centre, normal, outline, thickness, back=True):
    """Flat prism: `outline` = list of (u, v) in the plane at `centre`
    perpendicular to `normal`; extruded `thickness` along the normal."""
    m = Mesh()
    u, v = basis(normal)
    base = [add(centre, add(mul(u, a), mul(v, b))) for a, b in outline]
    top = [add(p, mul(normal, thickness)) for p in base]
    mid = add(centre, mul(normal, thickness * 0.5))
    m.poly(colour, top, inside=mid)
    if back:
        m.poly(colour, base, inside=mid)
    n = len(base)
    for i in range(n):
        m.tri(colour, base[i], base[(i + 1) % n], top[i], inside=mid)
        m.tri(colour, base[(i + 1) % n], top[(i + 1) % n], top[i], inside=mid)
    return m


def ngon(n, rx, ry, spin=0.0):
    return [(rx * math.cos(2 * math.pi * (k + spin) / n), ry * math.sin(2 * math.pi * (k + spin) / n)) for k in range(n)]


def head_dir(up_deg, around_deg):
    """Point on the unit head sphere: `up` degrees from the crown, `around`
    degrees from the back (-z) towards the avatar's right (-x) (the old
    avatar.c head_dir mirrored onto the new forward +z)."""
    u, a = math.radians(up_deg), math.radians(around_deg)
    return (-math.sin(u) * math.sin(a), math.cos(u), -math.sin(u) * math.cos(a))


# ---------------------------------------------------------------- hair
def short_cap():
    m = Mesh()
    for c, a, b, d in HAIR_TRIS:
        m.tri(C_PLAIN, a, b, d)
    m.ncap = len(m.tris)
    return m


def hair_bob():
    # old: cap tilted 35 deg back, 106 deg wide, 1.09 R: sides and back down to
    # the jaw, the face clear. The last ring flares a little.
    m = Mesh()
    R, tilt, ext, rings, segs = 0.272, 35.0, 106.0, 5, 10
    tl = math.radians(tilt)

    def pt(i, j):
        phi = math.radians(ext) * i / rings
        psi = 2 * math.pi * (j + 0.5 * (i % 2)) / segs
        rr = R * (1.0 + (0.035 if i == rings else 0.0))
        local = (math.sin(phi) * math.sin(psi), math.cos(phi), math.sin(phi) * math.cos(psi))
        return mul(rot_x(local, -tl), rr)

    o = (0.0, 0.0, 0.0)
    for j in range(segs):
        m.tri(C_PLAIN, pt(0, 0), pt(1, j), pt(1, j + 1), inside=o)
    for i in range(1, rings):
        sh = i % 2  # ring i is offset half a step: zig-zag triangles
        for j in range(segs):
            a, b = pt(i, j), pt(i, j + 1)
            c, d = pt(i + 1, j), pt(i + 1, j + 1)
            if sh:
                m.tri(C_PLAIN, a, b, d, inside=o)
                m.tri(C_PLAIN, a, d, c, inside=o)
            else:
                m.tri(C_PLAIN, a, b, c, inside=o)
                m.tri(C_PLAIN, b, d, c, inside=o)
    return m


def hair_bun():
    m = short_cap()
    d = head_dir(40.0, 0.0)
    m.extend(icosahedron(C_PLAIN, mul(d, 0.285), 0.105, squash=(1.0, 0.92, 1.0), turn=0.3))
    return m


SPIKES = [(0, 0), (35, 0), (35, 72), (35, 144), (35, 216), (35, 288), (62, 0)]


def hair_spiky():
    m = short_cap()
    for k, (up, around) in enumerate(SPIKES):
        d = head_dir(up, around)
        m.extend(pyramid(C_PLAIN, mul(d, 0.236), d, 0.062, 0.062, 0.185, 4, spin=0.125 * (k % 2)))
    return m


def hair_curly():
    m = cap(0.279, 24.07, 76.0, 4, 8, phase=0.5)
    m.ncap = len(m.tris)
    for ring in range(3):
        n = 1 if ring == 0 else ring * 6
        for k in range(n):
            up = ring * 30.0
            around = k * 360.0 / n + ring * 15.0
            d = head_dir(up, around)
            if d[2] > 0.55:
                continue  # keep the forehead clear
            m.extend(pyramid(C_PLAIN, mul(d, 0.252), d, 0.066, 0.066, 0.064, 4, spin=0.125 + 0.25 * (k % 2)))
    return m


# ---------------------------------------------------------------- body suit
CHEST = (0.128, 0.43)  # wearer's left chest: cross, pocket, badge share it

V_TOP_Y, V_HALF, V_APEX_Y = 0.588, 0.072, 0.45
LAPEL_W = 0.034
LAPEL_END_Y = 0.39


def suit_vneck(m):
    body_patch(m, C_VNECK, [(-V_HALF, V_TOP_Y), (0.0, V_APEX_Y), (V_HALF, V_TOP_Y)])


def suit_coat(m):
    # lapels: from the V's edges down to where the coat closes, wider outside
    # (two convex pieces each: the outline bends in at the V apex)
    for sx in (1.0, -1.0):
        apex, vtop = (0.0, V_APEX_Y), (sx * V_HALF, V_TOP_Y)
        outer, low, tip = (sx * (V_HALF + LAPEL_W), V_TOP_Y - 0.012), (sx * 0.036, LAPEL_END_Y + 0.012), (0.0, LAPEL_END_Y)
        body_patch(m, C_COAT, [apex, vtop, outer, low])
        body_patch(m, C_COAT, [apex, low, tip])
    # the opening line, centred on the front ridge
    body_patch(m, C_COAT, [(-0.0035, 0.04), (0.0035, 0.04), (0.0035, LAPEL_END_Y + 0.001), (-0.0035, LAPEL_END_Y + 0.001)])


def suit_buttons(m):
    # small faceted buttons on the opening line: a hexagon whose corners sit on
    # the facets (two on the ridge, so no edge crosses it) and a flat top
    for y in (0.335, 0.265, 0.195):
        r, h = 0.0125, 0.0085
        cz = body_z(0.0, y)
        base, top = [], []
        for k in range(6):
            a = math.radians(30 + 60 * k)
            x, yy = r * math.cos(a), y + r * math.sin(a)
            base.append((x, yy, body_z(x, yy) + 0.004))
            top.append((0.62 * r * math.cos(a), y + 0.62 * r * math.sin(a), cz + h))
        inside = (0.0, y, cz - 0.01)
        m.poly(C_BUTTON, top, inside=inside)
        for k in range(6):
            m.tri(C_BUTTON, base[k], base[(k + 1) % 6], top[k], inside=inside)
            m.tri(C_BUTTON, base[(k + 1) % 6], top[(k + 1) % 6], top[k], inside=inside)


def suit_cross(m):
    cx, cy = CHEST
    L, W = 0.027, 0.0085
    body_patch(m, C_CROSS, [(cx - W, cy - L), (cx + W, cy - L), (cx + W, cy + L), (cx - W, cy + L)])
    body_patch(m, C_CROSS, [(cx - L, cy - W), (cx - W, cy - W), (cx - W, cy + W), (cx - L, cy + W)])
    body_patch(m, C_CROSS, [(cx + W, cy - W), (cx + L, cy - W), (cx + L, cy + W), (cx + W, cy + W)])


def suit_pocket(m):
    cx, cy = CHEST
    hw, top, bot, flap = 0.04, cy + 0.04, cy - 0.04, 0.017
    body_patch(m, C_POCKET, [(cx - hw, bot), (cx + hw, bot), (cx + hw, top - flap), (cx - hw, top - flap)])
    body_patch(m, C_FLAP, [(cx - hw - 0.002, top - flap), (cx + hw + 0.002, top - flap), (cx + hw + 0.002, top),
                           (cx - hw - 0.002, top)])


def suit_badge(m):
    cx, cy = CHEST
    body_patch(m, C_BADGE, [(cx + x, cy + y) for x, y in ngon(10, 0.026, 0.026, 0.5)])


LATHE_YS = sorted({round(v[1], 6) for t in BODY_TRIS for v in t[1:]})
LATHE_STEP = 30.0  # avatar_body: 12 segments, a vertex column every 30 deg from +z


def lathe_surface(theta_deg, y):
    """Body surface at height y, direction theta (deg from +z towards +x)."""
    th = math.radians(theta_deg)
    d = (math.sin(th), 0.0, math.cos(th))
    return add((0.0, y, 0.0), mul(d, max(ray_hits(BODY.tris, (0.0, y, 0.0), d))))  # from the axis: the one exit


def lathe_offset(theta_deg, y, lift):
    """Surface point pushed out so it is `lift` from every facet that meets
    there (one on a face, two on a crease or ridge, four at a corner)."""
    p = lathe_surface(theta_deg, y)
    normals = []
    for dt in (-0.05, 0.05):
        for dy in (-0.0002, 0.0002):
            q = lathe_surface(theta_deg + dt, y + dy)
            n = body_normal_at(sub(q, mul(norm((q[0], 0.0, q[2])), 0.0005)))
            if all(dot(n, o) < 1 - 1e-6 for o in normals):
                normals.append(n)
    avg = (0.0, 0.0, 0.0)
    for n in normals:
        avg = add(avg, n)
    avg = norm(avg)
    return add(p, mul(avg, lift / min(dot(avg, n) for n in normals)))


def theta_of(x, y):
    return math.degrees(math.atan2(x, body_z(x, y)))


def lathe_path(ctrl, lift, closed=False):
    """Polyline over the body through control points (theta deg, y), split
    wherever it crosses a ridge or a crease: every piece then lies over a
    single facet, parallel to it, `lift` off it."""
    out = []
    n = len(ctrl)
    for i in range(n if closed else n - 1):
        (t0, y0), (t1, y1) = ctrl[i], ctrl[(i + 1) % n]
        ts = [0.0]
        for k in range(-12, 13):
            g = k * LATHE_STEP
            if (t0 - g) * (t1 - g) < 0:
                ts.append((g - t0) / (t1 - t0))
        for g in LATHE_YS:
            if (y0 - g) * (y1 - g) < 0:
                ts.append((g - y0) / (y1 - y0))
        for t in sorted(ts):
            out.append(lathe_offset(t0 + (t1 - t0) * t, y0 + (y1 - y0) * t, lift))
        if i == n - 2 and not closed:
            out.append(lathe_offset(t1, y1, lift))
    # drop points closer than 5 mm to the previous one
    tidy_pts = [out[0]]
    for p in out[1:]:
        if length(sub(p, tidy_pts[-1])) > 0.005:
            tidy_pts.append(p)
    return tidy_pts


def suit_stethoscope(m):
    r = 0.008
    lift = r + 0.004
    Y = (0.0, 0.40)
    neck = (60.0, 0.598)  # on a ridge: no extra split
    # one closed loop: Y -> up the wearer's left -> round the back of the neck
    # (on the shoulder slope, a point on every ridge) -> down the right -> Y
    ctrl = [Y, neck] + [(a, neck[1]) for a in (90.0, 120.0, 150.0, 180.0, 210.0, 240.0, 270.0)]
    ctrl += [(360.0 - neck[0], neck[1]), (360.0, Y[1])]
    pts = lathe_path(ctrl, lift)[:-1]  # theta 360 is the Y again
    m.extend(tube(C_TUBE, pts, r, 4, closed=True, spin=0.125))
    # the chest piece on the wearer's right, below the Y: an octagonal disc on one facet
    cx, cy = -0.058, 0.300
    cp = (cx, cy, body_z(cx, cy))
    n = body_normal_at(add(cp, (0, 0, -0.001)))
    centre = add(cp, mul(n, 0.003))
    thick = 0.011
    m.extend(prism(C_STEEL, centre, n, ngon(6, 0.023, 0.021, 0.0), thick, back=True))
    # the stem: from the Y down into the disc's top edge, capped inside the disc
    stem = lathe_path([(0.0, Y[1]), (theta_of(cx + 0.006, cy + 0.022), cy + 0.022)], 0.0045 + thick * 0.5)
    m.extend(tube(C_TUBE, stem, r * 0.85, 4, caps=(False, True), spin=0.125))


def body_suit():
    m = Mesh()
    for c, a, b, d in BODY_TRIS:
        m.tri(C_PLAIN, a, b, d)
    parts = {}
    for name, fn in (("vneck", suit_vneck), ("coat", suit_coat), ("buttons", suit_buttons), ("cross", suit_cross),
                     ("pocket", suit_pocket), ("badge", suit_badge), ("stethoscope", suit_stethoscope)):
        pm = Mesh()
        fn(pm)
        parts[name] = len(pm.tris)
        m.extend(pm)
    return m, parts


def body_hull():
    m = Mesh()
    for c, a, b, d in BODY_TRIS:
        m.tri(C_PLAIN, a, b, d)
    return m


# ---------------------------------------------------------------- accessories
def acc_sunglasses():
    m = Mesh()
    for s in (1.0, -1.0):
        d = norm((s * EYE[0], 0.006, EYE[2]))
        centre = mul(d, 0.281)
        lens = [(x, y) for x, y in ngon(8, 0.056, 0.042, 0.5)]
        lens = [(x * s, y) for x, y in lens]
        m.extend(prism(C_PLAIN, centre, d, lens if s > 0 else list(reversed(lens)), 0.009))
        # temple arm from the lens' outer edge back over the side of the head
        u, _ = basis(d)
        outer = add(add(centre, mul(u, s * 0.052)), mul(d, 0.004))
        arm = [outer]
        for az in (68.0, 98.0):
            a = math.radians(az)
            arm.append((s * 0.268 * math.sin(a), 0.012, 0.268 * math.cos(a)))
        m.extend(tube(C_PLAIN, arm, 0.0048, 4, caps=(False, True), spin=0.125))
    # bridge over the nose
    bridge = []
    for sx in (-1.0, 1.0):
        d = norm((sx * EYE[0], 0.006, EYE[2]))
        u, _ = basis(d)
        bridge.append(add(add(mul(d, 0.2855), mul(u, -sx * 0.05)), (0.0, 0.012, 0.0)))  # inside the lens' inner edge
    bridge.insert(1, (0.0, 0.022, 0.279))
    m.extend(tube(C_PLAIN, bridge, 0.0055, 4, spin=0.125))
    return m


def acc_cat_ears():
    m = Mesh()
    for s in (1.0, -1.0):
        el = math.radians(53.0)
        base_dir = norm((s * math.cos(el), math.sin(el), -0.08))
        base_c = mul(base_dir, 0.2)
        axis = norm(rot_z((0.0, 1.0, 0.0), -s * math.radians(22.0)))
        axis = norm(add(axis, (0.0, 0.0, 0.05)))
        ear = pyramid(C_PLAIN, base_c, axis, 0.078, 0.034, 0.19, 6, closed=True)
        m.extend(ear)
    return m


MASK_TOP, MASK_BOT, MASK_AZ = -0.046, -0.205, 72.0


def acc_mask():
    m = Mesh()
    sa, ca = math.sin(math.radians(MASK_AZ)), math.cos(math.radians(MASK_AZ))
    half = [
        lambda p: MASK_TOP - p[1],
        lambda p: p[1] - MASK_BOT,
        lambda p: p[2] * sa - p[0] * ca,
        lambda p: p[2] * sa + p[0] * ca,
    ]
    n = 4  # two pleats
    bands = []
    for k in range(n):
        top = MASK_TOP + (MASK_BOT - MASK_TOP) * k / n - (0.01 if k == 0 else 0.0)
        bot = MASK_TOP + (MASK_BOT - MASK_TOP) * (k + 1) / n - (0.01 if k == n - 1 else 0.0)
        bands.append((top, bot, C_PLAIN if k % 2 == 0 else C_PLEAT))
    head_patch(m, C_PLAIN, half, 1.016, bands)
    # ear loops: narrow great-circle bands from under the mask's side edge to
    # behind where an ear would be, on a copy just inside the mask's
    for s in (1.0, -1.0):
        for y0, y1 in ((-0.05, -0.012), (-0.195, -0.07)):
            p0 = norm((s * 0.25 * math.sin(math.radians(68.0)), y0, 0.25 * math.cos(math.radians(68.0))))
            p1 = norm((s * 0.25 * math.sin(math.radians(102.0)), y1, 0.25 * math.cos(math.radians(102.0))))
            nrm = norm(cross(p0, p1))
            w = 0.0055
            a0, a1 = math.radians(66.0), math.radians(102.0)
            hs = [
                lambda p, nrm=nrm: w - dot(nrm, p),
                lambda p, nrm=nrm: w + dot(nrm, p),
                # azimuth between a0 and a1 on this side
                lambda p, s=s: s * p[0] * math.cos(a0) - p[2] * math.sin(a0),
                lambda p, s=s: p[2] * math.sin(a1) - s * p[0] * math.cos(a1),
            ]
            head_patch(m, C_PLAIN, hs, 1.011)
    return m


def npc_hat():
    m = Mesh()
    segs = 10
    y0 = 0.2
    crown_r, brim_r = 0.19, 0.335
    inner_y, outer_y, th = y0 + 0.03, y0 - 0.002, 0.016
    crown_top = y0 + 0.145

    def ring(r, y, k_off=0.0):
        return [(r * math.sin(2 * math.pi * (k + k_off) / segs), y, r * math.cos(2 * math.pi * (k + k_off) / segs))
                for k in range(segs)]

    it, ot = ring(crown_r, inner_y), ring(brim_r, outer_y)
    ib, ob = ring(crown_r, inner_y - th), ring(brim_r, outer_y - th)
    ct = ring(crown_r * 0.86, crown_top)
    for k in range(segs):
        j = (k + 1) % segs
        # brim top (up), brim underside (down), brim edge (out)
        m.tri(C_PLAIN, it[k], ot[k], ot[j], outside=add(lerp(it[k], ot[j], 0.5), (0, 1, 0)))
        m.tri(C_PLAIN, it[k], ot[j], it[j], outside=add(lerp(it[k], ot[j], 0.5), (0, 1, 0)))
        m.tri(C_PLAIN, ib[k], ob[k], ob[j], outside=add(lerp(ib[k], ob[j], 0.5), (0, -1, 0)))
        m.tri(C_PLAIN, ib[k], ob[j], ib[j], outside=add(lerp(ib[k], ob[j], 0.5), (0, -1, 0)))
        c = (0.0, outer_y - th * 0.5, 0.0)
        m.tri(C_PLAIN, ot[k], ob[k], ob[j], inside=c)
        m.tri(C_PLAIN, ot[k], ob[j], ot[j], inside=c)
        # crown side
        cc = (0.0, inner_y, 0.0)
        m.tri(C_PLAIN, it[k], it[j], ct[k], inside=cc)
        m.tri(C_PLAIN, it[j], ct[j], ct[k], inside=cc)
    m.poly(C_PLAIN, ct, outside=(0.0, 1.0, 0.0))
    m.poly(C_PLAIN, ib, outside=(0.0, -1.0, 0.0))
    return m


def npc_cane():
    pts = [(0.0, 0.0, 0.0), (0.0, 0.84, 0.0), (0.0, 0.878, 0.028), (0.0, 0.884, 0.125)]
    return tube(C_PLAIN, pts, 0.017, 6, caps=(True, True))


def npc_bucket():
    m = Mesh()
    segs = 10
    rb, rt, h, wall = 0.078, 0.09, 0.16, 0.007

    def ring(r, y):
        return [(r * math.sin(2 * math.pi * k / segs), y, r * math.cos(2 * math.pi * k / segs)) for k in range(segs)]

    ob, ot = ring(rb, 0.0), ring(rt, h)
    ib, it = ring(rb - wall, wall), ring(rt - wall, h)
    axis_mid = (0.0, h * 0.5, 0.0)
    for k in range(segs):
        j = (k + 1) % segs
        m.tri(C_PLAIN, ob[k], ob[j], ot[k], inside=axis_mid)
        m.tri(C_PLAIN, ob[j], ot[j], ot[k], inside=axis_mid)
        m.tri(C_PLAIN, ib[k], ib[j], it[k], outside=axis_mid)  # inner wall faces the axis
        m.tri(C_PLAIN, ib[j], it[j], it[k], outside=axis_mid)
        m.tri(C_PLAIN, ot[k], ot[j], it[k], outside=(0.0, 1.0, 0.0))  # rim
        m.tri(C_PLAIN, ot[j], it[j], it[k], outside=(0.0, 1.0, 0.0))
    m.poly(C_PLAIN, ob, outside=(0.0, -1.0, 0.0))
    m.poly(C_PLAIN, ib, outside=(0.0, 1.0, 0.0))
    # handle: an arc over the top, its ends on the outer wall
    arc = []
    for k in range(5):
        a = math.pi * k / 4
        arc.append((0.093 * math.cos(a), h - 0.018 + 0.1 * math.sin(a), 0.0))
    m.extend(tube(C_PLAIN, arc, 0.0055, 4, caps=(True, True), spin=0.125))
    return m


# ---------------------------------------------------------------- output
def fmt(x):
    s = f"{x:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def rounded(mesh):
    out = []
    for c, a, b, d in mesh.tris:
        r = tuple(tuple(float(fmt(x)) for x in p) for p in (a, b, d))
        if length(tri_normal(*r)) < 2e-9:
            continue  # degenerate after rounding to 0.1 mm
        out.append((c,) + r)
    return out


def build():
    suit, suit_parts = body_suit()
    groups = [
        ("hair_bob", hair_bob(), "head"),
        ("hair_bun", hair_bun(), "head"),
        ("hair_spiky", hair_spiky(), "head"),
        ("hair_curly", hair_curly(), "head"),
        ("body_suit", suit, "body"),
        ("body_suit_hull", body_hull(), "body"),
        ("acc_sunglasses", acc_sunglasses(), "head"),
        ("acc_cat_ears", acc_cat_ears(), "head"),
        ("acc_mask", acc_mask(), "head"),
        ("npc_hat", npc_hat(), "head"),
        ("npc_cane", npc_cane(), "root"),
        ("npc_bucket", npc_bucket(), "root"),
    ]
    caps = {n: (m.ncap if m.ncap is not None else len(m.tris)) for n, m, _ in groups}
    return [(n, rounded(m), sp) for n, m, sp in groups], suit_parts, caps


def write(path, groups):
    lines = [
        "# avatar_options.mgmesh : generated by tools/gen_avatar_options.py from avatar.mgmesh, do not edit.",
        "# mgmesh 1, metres, y-up, forward +z, the avatar's left is +x. Palette 'c <i> <rrggbb>:<zone>' (meshgen: alpha = 255 - zone).",
        "# head space: origin at the head centre. body space: origin at the body base. npc_cane / npc_bucket: origin on the ground.",
        "# body_suit zones: 0 body, 1 V-neck, 2 coat front, 3 coat buttons, 4 cross, 5 pocket, 6 badge, 7 stethoscope.",
    ]
    for i, rgb, zone in PALETTE:
        lines.append(f"c {i} {rgb}:{zone}" if zone else f"c {i} {rgb}")
    for name, tris, _ in groups:
        lines.append(f"g {name}")
        for c, a, b, d in tris:
            lines.append("t %d %s" % (c, " ".join(fmt(x) for p in (a, b, d) for x in p)))
    data = "\n".join(lines) + "\n"
    with open(path + ".tmp", "w", encoding="utf-8", newline="\n") as f:
        f.write(data)
    os.replace(path + ".tmp", path)


# ---------------------------------------------------------------- checks
def samples(a, b, c, n=4):
    for i in range(n + 1):
        for j in range(n + 1 - i):
            k = n - i - j
            yield ((a[0] * i + b[0] * j + c[0] * k) / n, (a[1] * i + b[1] * j + c[1] * k) / n,
                   (a[2] * i + b[2] * j + c[2] * k) / n)


def min_clearance(tris, convex, pick=None):
    lo = 1.0
    for t in tris:
        if pick is not None and not pick(t):
            continue
        for p in samples(t[1], t[2], t[3]):
            lo = min(lo, convex.dist(p))
    return lo


def hair_enclosure(tris, cover_tris):
    """For every head vertex under the hair: the hair along the ray from the
    head centre must be >= 2 mm further out. Returns (checked, worst margin)."""
    shell = [(t[1], t[2], t[3]) for t in cover_tris]
    verts = sorted({v for t in HEAD_FACETS for v in t})
    worst, checked = 1.0, 0
    for v in verts:
        d = norm(v)
        hits = ray_hits(shell, (0.0, 0.0, 0.0), d)
        if not hits:
            continue
        checked += 1
        worst = min(worst, min(hits) - length(v))
    return checked, worst


def check(groups, suit_parts, caps):
    ok = True
    limits = {"body_suit": None, "body_suit_hull": None}
    print("group              tris  clearance")
    by = {n: t for n, t, _ in groups}
    for name, tris, space in groups:
        lim = limits.get(name, 120)
        note = ""
        if name.startswith("hair_"):
            cover = [t for t in tris if t[0] == C_PLAIN]
            # the shared cap: triangles whose centroid is <= 0.29 m from the centre
            capt = tris[:caps[name]]
            n_v, worst = hair_enclosure(tris, capt)
            clr = min_clearance(capt, HEAD)
            note = f"cap >= {clr * 1000:.1f} mm off the head; {n_v} head vertices under it, worst {worst * 1000:.1f} mm"
            if worst < 0.002 or clr < 0.0015:
                ok = False
                note += "  <-- FAIL"
        elif name == "body_suit":
            det = tris[len(BODY_TRIS):]
            clr = min_clearance(det, BODY)
            clr_h = min_clearance([t for t in det if t[0] == C_TUBE], HEAD_IN_BODY)
            note = f"details >= {clr * 1000:.1f} mm off the body, stethoscope >= {clr_h * 1000:.1f} mm off the head; " + \
                ", ".join(f"{k} {v}" for k, v in suit_parts.items())
            for k, v in suit_parts.items():
                if v > (150 if k == "stethoscope" else 60):
                    ok = False
                    note += f"  <-- {k} over budget"
            if clr < 0.0015 or clr_h < 0.0015:
                ok = False
                note += "  <-- FAIL"
        elif name in ("acc_sunglasses", "acc_mask"):
            clr = min_clearance(tris, HEAD)
            note = f">= {clr * 1000:.1f} mm off the head"
            if clr < 0.0015:
                ok = False
                note += "  <-- FAIL"
        elif name == "acc_cat_ears":
            # the base ring must be buried in the head (it is closed anyway)
            worst = max(HEAD.dist(p) for t in tris for p in t[1:] if p[1] < 0.2)
            note = f"base ring {worst * 1000:.1f} mm (negative = inside the head)"
            if worst > -0.002:
                ok = False
                note += "  <-- FAIL"
        if lim is not None and len(tris) > lim:
            ok = False
            note += f"  <-- over {lim}"
        print(f"{name:18s} {len(tris):5d}  {note}")
    if by["body_suit_hull"] != [(C_PLAIN,) + t[1:] for t in BODY_TRIS]:
        print("body_suit_hull differs from avatar_body")
    return ok


# ---------------------------------------------------------------- preview
SKIN, HAIR, EYE_C = 0xF2C9A5, 0x5A3A22, 0x1B2A2A


def rgbf(h):
    return ((h >> 16) & 255) / 255.0, ((h >> 8) & 255) / 255.0, (h & 255) / 255.0


PAL = {i: rgbf(int(rgb, 16)) for i, rgb, _ in PALETTE}
ZONE = {i: z for i, _, z in PALETTE}


def tinted(tris, tint_of, offset=(0.0, 0.0, 0.0), skip_zones=()):
    out = []
    for c, a, b, d in tris:
        z = ZONE.get(c, 0)
        if z in skip_zones:
            continue
        t = tint_of(z)
        if t is None:
            continue
        v = PAL.get(c, (1, 1, 1))
        col = (v[0] * t[0], v[1] * t[1], v[2] * t[2])
        out.append((col, add(a, offset), add(b, offset), add(d, offset)))
    return out


def base_avatar(hair=True, eyes=True, body=True):
    head_off = (0.0, HEAD_Y, 0.0)
    out = []
    if body:
        out += tinted(BODY_TRIS, lambda z: rgbf(0x4A78D0))
        for s in (1, -1):
            hand = SRC_GROUPS["avatar_hand"]
            out += tinted(hand, lambda z: rgbf(SKIN), (s * 0.34, 0.28, 0.04))
    out += tinted(HEAD_TRIS, lambda z: rgbf(SKIN), head_off)
    if eyes:
        for s in (1, -1):
            out += tinted(SRC_GROUPS["avatar_eye"], lambda z: rgbf(EYE_C), (s * EYE[0], HEAD_Y + EYE[1], EYE[2]))
    if hair:
        out += tinted(HAIR_TRIS, lambda z: rgbf(HAIR), head_off)
    return out


def render(tris, yaw_deg, pitch_deg, centre, extent, W, H, img, ox, oy):
    yaw, pitch = math.radians(yaw_deg), math.radians(pitch_deg)
    c = (math.sin(yaw) * math.cos(pitch), math.sin(pitch), math.cos(yaw) * math.cos(pitch))
    r = norm(cross((0.0, 1.0, 0.0), c))
    u = cross(c, r)
    light = norm((-0.55, 1.0, 0.5))
    scale = min(W, H) / extent
    zbuf = [[-1e9] * W for _ in range(H)]
    for col, a, b, d in tris:
        n = tri_normal(a, b, d)
        if length(n) < 1e-12:
            continue
        n = norm(n)
        if dot(n, c) <= 0:
            continue  # back face
        lam = dot(n, light)
        shade = 0.55 + (0.45 if lam > 0.41 else 0.25 if lam > 0.0 else 0.0) + 0.1 * lam
        pc = (min(1, col[0] * shade), min(1, col[1] * shade), min(1, col[2] * shade))
        pts = []
        for p in (a, b, d):
            q = sub(p, centre)
            pts.append((W / 2 + dot(q, r) * scale, H / 2 - dot(q, u) * scale, dot(q, c)))
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        x0, x1 = max(0, int(min(xs))), min(W - 1, int(max(xs)) + 1)
        y0, y1 = max(0, int(min(ys))), min(H - 1, int(max(ys)) + 1)
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = pts
        area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
        if abs(area) < 1e-9:
            continue
        for py in range(y0, y1 + 1):
            fy = py + 0.5
            for px in range(x0, x1 + 1):
                fx = px + 0.5
                w0 = ((bx - fx) * (cy - fy) - (by - fy) * (cx - fx)) / area
                w1 = ((cx - fx) * (ay - fy) - (cy - fy) * (ax - fx)) / area
                w2 = 1 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0:
                    continue
                z = w0 * az + w1 * bz + w2 * cz
                if z > zbuf[py][px]:
                    zbuf[py][px] = z
                    img[oy + py][ox + px] = pc
    # dark silhouette edges so floating gaps show
    for py in range(H):
        for px in range(W):
            zc = zbuf[py][px]
            if zc < -1e8:
                continue
            for qx, qy in ((px + 1, py), (px, py + 1), (px - 1, py), (px, py - 1)):
                if 0 <= qx < W and 0 <= qy < H and zbuf[qy][qx] < zc - 0.02:
                    img[oy + qy][ox + qx] = (0.0, 0.0, 0.0)


def write_png(path, img):
    H, W = len(img), len(img[0])
    raw = bytearray()
    for row in img:
        raw.append(0)
        for r, g, b in row:
            raw += bytes((int(r * 255 + 0.5), int(g * 255 + 0.5), int(b * 255 + 0.5)))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def preview(path, groups):
    by = {n: t for n, t, _ in groups}
    head_off = (0.0, HEAD_Y, 0.0)
    hair_t = lambda z: rgbf(HAIR)
    coat = {0: rgbf(0xF5F7FA), 1: rgbf(0x12A594), 2: rgbf(0xF5F7FA), 3: (1, 1, 1), 4: rgbf(0xE04848), 7: (1, 1, 1)}
    scrubs = {0: rgbf(0x12A594), 1: rgbf(0x0E7F72), 5: rgbf(0x12A594), 7: (1, 1, 1)}
    plain = {0: rgbf(0x4A78D0), 6: rgbf(0xF0C040)}

    def suit(tints):
        return tinted(by["body_suit"], lambda z: tints.get(z))

    def avatar_with(suit_tints, hair_tris=None, extra=(), hair=True):
        out = suit(suit_tints)
        for s in (1, -1):
            out += tinted(SRC_GROUPS["avatar_hand"], lambda z: rgbf(SKIN), (s * 0.34, 0.28, 0.04))
        out += base_avatar(hair=hair and hair_tris is None, body=False)
        if hair_tris is not None:
            out += tinted(hair_tris, hair_t, head_off)
        return out + list(extra)

    head_only = lambda extra, hair=False, eyes=True: base_avatar(hair=hair, eyes=eyes, body=False) + extra
    rows = [
        ("hair_bob", head_only(tinted(by["hair_bob"], hair_t, head_off)), "head"),
        ("hair_bun", head_only(tinted(by["hair_bun"], hair_t, head_off)), "head"),
        ("hair_spiky", head_only(tinted(by["hair_spiky"], hair_t, head_off)), "head"),
        ("hair_curly", head_only(tinted(by["hair_curly"], hair_t, head_off)), "head"),
        ("coat", avatar_with(coat), "body"),
        ("coat_close", suit(coat) + base_avatar(body=False), "chest"),
        ("scrubs_pocket", suit(scrubs) + base_avatar(body=False), "chest"),
        ("plain_badge", suit(plain) + base_avatar(body=False), "chest"),
        ("sunglasses", head_only(tinted(by["acc_sunglasses"], lambda z: rgbf(0x23222B), head_off), hair=True, eyes=False), "head"),
        ("cat_ears", head_only(tinted(by["acc_cat_ears"], hair_t, head_off) + tinted(by["hair_bob"], hair_t, head_off)), "head"),
        ("mask", head_only(tinted(by["acc_mask"], lambda z: rgbf(0x9FD3EE), head_off), hair=True), "head"),
        ("hat", head_only(tinted(by["npc_hat"], lambda z: rgbf(0xE8D8A0), head_off), hair=True), "head"),
        ("cane_bucket", avatar_with(plain) + tinted(by["npc_cane"], lambda z: rgbf(0x6B4A2E), (-0.37, -0.42, 0.06))
         + tinted(by["npc_bucket"], lambda z: rgbf(0x9AA0A6), (0.34, 0.28 - 0.235 - 0.085, 0.04)), "body"),
    ]
    views = [(0, 8), (35, 12), (90, 6), (150, 15), (210, 10)]
    W = H = 190
    img = [[(0.93, 0.94, 0.96)] * (W * len(views)) for _ in range(H * len(rows))]
    frame = {"head": ((0.0, HEAD_Y + 0.03, 0.0), 0.78), "body": ((0.0, 0.5, 0.0), 1.9),
             "chest": ((0.0, 0.45, 0.1), 0.62)}
    for ri, (name, tris, fr) in enumerate(rows):
        centre, ext = frame[fr]
        for vi, (yaw, pitch) in enumerate(views):
            if fr == "chest":
                yaw = (0, 25, 50, 90, -30)[vi]
            render(tris, yaw, pitch, centre, ext, W, H, img, vi * W, ri * H)
        print("preview row", ri, name, file=sys.stderr)
    write_png(path, img)


def main():
    args = sys.argv[1:]
    out, prev, do_check = DST, None, True
    while args:
        a = args.pop(0)
        if a == "--out":
            out = args.pop(0)
        elif a == "--preview":
            prev = args.pop(0)
        elif a == "--no-check":
            do_check = False
        else:
            sys.stderr.write(__doc__)
            sys.exit(2)
    groups, suit_parts, caps = build()
    if do_check and not check(groups, suit_parts, caps):
        sys.stderr.write("gen_avatar_options: checks failed\n")
        sys.exit(1)
    write(out, groups)
    if prev:
        preview(prev, groups)


if __name__ == "__main__":
    main()
