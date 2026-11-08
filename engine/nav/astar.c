/* A* on one grid (8 neighbours, no corner cutting, octile heuristic, +0.5
 * next to walls), string-pulled with grid line of sight; a path between
 * storeys is two searches joined by an elevator ride. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/time.h"
#include "nav_internal.h"

#define SQRT2 1.41421356f

typedef struct Cellp {
    int x, z;
} Cellp;

static bool walkable(const NavGrid *g, int x, int z)
{
    return x >= 0 && z >= 0 && x < g->d.cols && z < g->d.rows && (g->flags[z * g->d.cols + x] & NAV_WALKABLE);
}

/* The walkable cell nearest (x, z) within 1 m; false if none. */
static bool nearest_walkable(const NavGrid *g, float wx, float wz, Cellp *out)
{
    int cx = (int)floorf((wx - g->d.x0) / NAV_CELL), cz = (int)floorf((wz - g->d.z0) / NAV_CELL);
    int best = -1;
    for (int dz = -4; dz <= 4; dz++) {
        for (int dx = -4; dx <= 4; dx++) {
            int d2 = dx * dx + dz * dz;
            if (d2 <= 16 && (best < 0 || d2 < best) && walkable(g, cx + dx, cz + dz)) {
                best = d2;
                out->x = cx + dx;
                out->z = cz + dz;
            }
        }
    }
    return best >= 0;
}

static float octile(int ax, int az, int bx, int bz)
{
    int dx = abs(ax - bx), dz = abs(az - bz);
    int mn = dx < dz ? dx : dz, mx = dx < dz ? dz : dx;
    return (float)(mx - mn) + SQRT2 * (float)mn;
}

/* binary min-heap of (f, node) with lazy deletion */
typedef struct Heap {
    int32_t *id;
    float *f;
    int n, cap;
} Heap;

static void heap_push(Heap *h, int32_t id, float f)
{
    if (h->n >= h->cap) {
        return;
    }
    int i = h->n++;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h->f[p] <= f) {
            break;
        }
        h->f[i] = h->f[p];
        h->id[i] = h->id[p];
        i = p;
    }
    h->f[i] = f;
    h->id[i] = id;
}

static int32_t heap_pop(Heap *h)
{
    int32_t top = h->id[0];
    float f = h->f[--h->n];
    int32_t id = h->id[h->n];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= h->n) {
            break;
        }
        if (c + 1 < h->n && h->f[c + 1] < h->f[c]) {
            c++;
        }
        if (h->f[c] >= f) {
            break;
        }
        h->f[i] = h->f[c];
        h->id[i] = h->id[c];
        i = c;
    }
    h->f[i] = f;
    h->id[i] = id;
    return top;
}

/* Line of sight on the grid: every cell a thin line crosses is walkable. */
static bool line_clear(const NavGrid *g, Cellp a, Cellp b)
{
    int dx = abs(b.x - a.x), dz = abs(b.z - a.z);
    int sx = a.x < b.x ? 1 : -1, sz = a.z < b.z ? 1 : -1;
    int err = dx - dz, x = a.x, z = a.z;
    while (x != b.x || z != b.z) {
        if (!walkable(g, x, z)) {
            return false;
        }
        int e2 = 2 * err;
        if (e2 > -dz && e2 < dx) { /* diagonal step: both side cells must be free too */
            if (!walkable(g, x + sx, z) || !walkable(g, x, z + sz)) {
                return false;
            }
        }
        if (e2 > -dz) {
            err -= dz;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            z += sz;
        }
    }
    return walkable(g, b.x, b.z);
}

static void add_point(NavPath *p, const NavGrid *g, Cellp c, int storey)
{
    if (p->count >= NAV_MAX_POINTS) {
        return;
    }
    NavPoint *np = &p->pts[p->count++];
    np->pos = HMM_V3(g->d.x0 + ((float)c.x + 0.5f) * NAV_CELL, g->d.floor_y, g->d.z0 + ((float)c.z + 0.5f) * NAV_CELL);
    bool outdoor = (g->flags[c.z * g->d.cols + c.x] & NAV_OUTDOOR) != 0;
    np->storey = (int8_t)(outdoor ? -1 : storey);
}

/* A* from a to b on grid g, string-pulled, appended to the path. */
static bool search(NavWorld *n, const NavGrid *g, Cellp a, Cellp b, int storey, NavPath *path, bool first)
{
    n->generation += 2;
    const uint32_t open_mark = n->generation, closed_mark = n->generation + 1;
    Heap h = {n->heap, n->heap_f, 0, n->total * 8};
    const int cols = g->d.cols;
    int start = g->base + a.z * cols + a.x, goal = g->base + b.z * cols + b.x;
    n->g[start] = 0.0f;
    n->parent[start] = -1;
    n->stamp[start] = open_mark;
    heap_push(&h, start, octile(a.x, a.z, b.x, b.z));
    bool found = false;
    while (h.n > 0) {
        int32_t cur = heap_pop(&h);
        if (n->stamp[cur] == closed_mark) {
            continue; /* stale heap entry */
        }
        n->stamp[cur] = closed_mark;
        if (cur == goal) {
            found = true;
            break;
        }
        int local = cur - g->base, cz = local / cols, cx = local - cz * cols;
        for (int dz = -1; dz <= 1; dz++) {
            for (int dx = -1; dx <= 1; dx++) {
                if ((dx == 0 && dz == 0) || !walkable(g, cx + dx, cz + dz)) {
                    continue;
                }
                if (dx != 0 && dz != 0 && (!walkable(g, cx + dx, cz) || !walkable(g, cx, cz + dz))) {
                    continue; /* no corner cutting */
                }
                int next = g->base + (cz + dz) * cols + cx + dx;
                if (n->stamp[next] == closed_mark) {
                    continue;
                }
                float step = (dx != 0 && dz != 0) ? SQRT2 : 1.0f;
                if (g->flags[(cz + dz) * cols + cx + dx] & NAV_NEAR_WALL) {
                    step += 0.5f;
                }
                float cost = n->g[cur] + step;
                if (n->stamp[next] != open_mark || cost < n->g[next]) {
                    n->g[next] = cost;
                    n->parent[next] = cur;
                    n->stamp[next] = open_mark;
                    heap_push(&h, next, cost + octile(cx + dx, cz + dz, b.x, b.z));
                }
            }
        }
    }
    if (!found) {
        return false;
    }
    /* walk back, then keep only the corners the line of sight needs */
    int len = 0;
    for (int c = goal; c != -1; c = n->parent[c]) {
        n->heap[len++] = c; /* the heap is free now: reuse it as the cell list */
    }
    Cellp anchor = a;
    if (first) {
        add_point(path, g, a, storey);
    }
    int i = len - 1; /* start */
    while (i > 0) {
        int j = 0; /* goal end: farthest visible cell from the anchor */
        for (; j < i; j++) {
            int local = n->heap[j] - g->base;
            Cellp c = {local % cols, local / cols};
            if (line_clear(g, anchor, c)) {
                break;
            }
        }
        if (j >= i) {
            j = i - 1; /* neighbouring cell: always reachable */
        }
        int local = n->heap[j] - g->base;
        anchor.x = local % cols;
        anchor.z = local / cols;
        add_point(path, g, anchor, storey);
        i = j;
    }
    return true;
}

static float path_length(const NavPath *p)
{
    float sum = 0.0f;
    for (int i = 1; i < p->count; i++) {
        HMM_Vec3 d = HMM_SubV3(p->pts[i].pos, p->pts[i - 1].pos);
        if (p->pts[i].storey == p->pts[i - 1].storey || i != p->ride_at + 1) {
            sum += sqrtf(d.X * d.X + d.Z * d.Z);
        }
    }
    return sum;
}

int nav_find_path(NavWorld *n, HMM_Vec3 from_pos, int from_storey, HMM_Vec3 to_pos, int to_storey, NavPath *path)
{
    double t0 = mg_time_now();
    path->count = 0;
    path->ride_at = -1;
    path->ride_from = path->ride_to = 0;
    path->length = 0.0f;
    int gfrom = nav_grid_index(n, from_storey), gto = nav_grid_index(n, to_storey);
    int result = NAV_UNREACHABLE;
    if (gfrom >= 0 && gto >= 0) {
        const NavGrid *ga = &n->grids[gfrom], *gb = &n->grids[gto];
        Cellp a, b;
        if (nearest_walkable(ga, from_pos.X, from_pos.Z, &a) && nearest_walkable(gb, to_pos.X, to_pos.Z, &b)) {
            if (gfrom == gto) {
                result = search(n, ga, a, b, gfrom, path, true) ? 0 : NAV_UNREACHABLE;
            } else {
                /* via the car: its centre on both storeys */
                float cx = (n->car[0] + n->car[2]) * 0.5f, cz = (n->car[1] + n->car[3]) * 0.5f;
                Cellp ca, cb;
                if (nearest_walkable(ga, cx, cz, &ca) && nearest_walkable(gb, cx, cz, &cb) &&
                    search(n, ga, a, ca, gfrom, path, true)) {
                    path->ride_at = path->count - 1;
                    path->ride_from = gfrom;
                    path->ride_to = gto;
                    add_point(path, gb, cb, gto);
                    result = search(n, gb, cb, b, gto, path, false) ? 0 : NAV_UNREACHABLE;
                }
            }
        }
    }
    if (result == 0) {
        path->length = path_length(path);
    } else {
        path->count = 0;
        path->ride_at = -1;
    }
    n->path_ms = (float)((mg_time_now() - t0) * 1000.0);
    return result;
}
