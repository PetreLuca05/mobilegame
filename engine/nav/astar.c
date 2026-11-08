/* A* on one grid (8 neighbours, no corner cutting, octile heuristic, +0.5
 * next to walls), string-pulled with grid line of sight; a path between
 * storeys is two searches joined by an elevator ride. Everything is resumable:
 * nav_service runs the queue in slices of at most its budget. */
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "core/time.h"
#include "nav_internal.h"

#define SQRT2 1.41421356f
/* Slightly over-weighted heuristic: far fewer cells searched; string-pulling
 * removes the small detours it can add. */
static const float k_h_weight = 1.2f;
#define CLOCK_EVERY 24 /* cells expanded between looks at the clock */

static bool walkable(const NavGrid *g, int x, int z)
{
    return x >= 0 && z >= 0 && x < g->d.cols && z < g->d.rows && (g->flags[z * g->d.cols + x] & NAV_WALKABLE);
}

static NavCell cell_at(const NavGrid *g, float wx, float wz)
{
    return (NavCell){(int)floorf((wx - g->d.x0) / NAV_CELL), (int)floorf((wz - g->d.z0) / NAV_CELL)};
}

/* The walkable cell nearest (x, z) within 1 m; false if none. */
static bool nearest_walkable(const NavGrid *g, float wx, float wz, NavCell *out)
{
    NavCell c = cell_at(g, wx, wz);
    int best = -1;
    for (int dz = -4; dz <= 4; dz++) {
        for (int dx = -4; dx <= 4; dx++) {
            int d2 = dx * dx + dz * dz;
            if (d2 <= 16 && (best < 0 || d2 < best) && walkable(g, c.x + dx, c.z + dz)) {
                best = d2;
                out->x = c.x + dx;
                out->z = c.z + dz;
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
static void heap_push(NavSearchMem *m, int *n, int cap, int32_t id, float f)
{
    if (*n >= cap) {
        return;
    }
    int i = (*n)++;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (m->heap_f[p] <= f) {
            break;
        }
        m->heap_f[i] = m->heap_f[p];
        m->heap[i] = m->heap[p];
        i = p;
    }
    m->heap_f[i] = f;
    m->heap[i] = id;
}

static int32_t heap_pop(NavSearchMem *m, int *n)
{
    int32_t top = m->heap[0];
    float f = m->heap_f[--*n];
    int32_t id = m->heap[*n];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= *n) {
            break;
        }
        if (c + 1 < *n && m->heap_f[c + 1] < m->heap_f[c]) {
            c++;
        }
        if (m->heap_f[c] >= f) {
            break;
        }
        m->heap_f[i] = m->heap_f[c];
        m->heap[i] = m->heap[c];
        i = c;
    }
    m->heap_f[i] = f;
    m->heap[i] = id;
    return top;
}

/* Line of sight on the grid: every cell a thin line crosses is walkable. */
static bool line_clear(const NavGrid *g, NavCell a, NavCell b)
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

static NavPoint point_at(const NavGrid *g, NavCell c, int storey)
{
    NavPoint np;
    np.pos = HMM_V3(g->d.x0 + ((float)c.x + 0.5f) * NAV_CELL, g->d.floor_y, g->d.z0 + ((float)c.z + 0.5f) * NAV_CELL);
    bool outdoor = (g->flags[c.z * g->d.cols + c.x] & NAV_OUTDOOR) != 0;
    np.storey = (int8_t)(outdoor ? -1 : storey);
    return np;
}

static void add_point(NavPath *p, const NavGrid *g, NavCell c, int storey)
{
    if (p->count < NAV_MAX_POINTS) {
        p->pts[p->count++] = point_at(g, c, storey);
    }
}

static NavCell node_cell(const NavGrid *g, int32_t node)
{
    int local = node - g->base;
    return (NavCell){local % g->d.cols, local / g->d.cols};
}

static void search_begin(NavSearch *s, NavSearchMem *m, const NavGrid *g, NavCell a, NavCell b, int storey, NavPath *path,
                         bool first)
{
    memset(s, 0, sizeof(*s));
    s->m = m;
    s->g = g;
    s->a = a;
    s->b = b;
    s->storey = storey;
    s->path = path;
    s->first = first;
    if (g->region[a.z * g->d.cols + a.x] != g->region[b.z * g->d.cols + b.x]) {
        s->phase = NAV_PHASE_FAILED; /* different connected areas: no need to flood the grid */
        return;
    }
    m->generation += 2;
    s->open_mark = m->generation;
    s->closed_mark = m->generation + 1;
    const int cols = g->d.cols;
    int start = g->base + a.z * cols + a.x;
    s->goal = g->base + b.z * cols + b.x;
    s->best = start;
    s->best_h = 1e9f;
    m->g[start] = 0.0f;
    m->parent[start] = -1;
    m->stamp[start] = s->open_mark;
    heap_push(m, &s->heap_n, g->d.cols * g->d.rows * 4, start, k_h_weight * octile(a.x, a.z, b.x, b.z));
    s->phase = NAV_PHASE_SEARCH;
}

/* Walk back from `end` into the heap array: cell list, goal end first. */
static int collect_cells(const NavSearch *s, int32_t end)
{
    int len = 0;
    for (int c = end; c != -1; c = s->m->parent[c]) {
        s->m->heap[len++] = c;
    }
    return len;
}

static void pull_begin(NavSearch *s, int32_t end)
{
    s->list_len = collect_cells(s, end);
    s->anchor = s->a;
    if (s->first) {
        add_point(s->path, s->g, s->a, s->storey);
    }
    s->i = s->list_len - 1; /* start */
    s->phase = NAV_PHASE_PULL;
}

/* From the anchor forwards to the farthest cell still in sight. */
static void pull_step(NavSearch *s)
{
    const NavGrid *g = s->g;
    int j = s->i - 1;
    while (j > 0 && line_clear(g, s->anchor, node_cell(g, s->m->heap[j - 1]))) {
        j--;
    }
    if (j < 0) {
        s->i = 0;
    } else {
        s->anchor = node_cell(g, s->m->heap[j]);
        add_point(s->path, g, s->anchor, s->storey);
        s->i = j;
    }
    if (s->i <= 0) {
        s->phase = NAV_PHASE_DONE;
    }
}

/* Run until done or the deadline (0 = no limit). */
static void search_run(NavSearch *s, double deadline)
{
    NavSearchMem *m = s->m;
    const NavGrid *g = s->g;
    const int cols = g->d.cols, cap = g->d.cols * g->d.rows * 4;
    int ticks = 0;
    while (s->phase == NAV_PHASE_SEARCH) {
        if (deadline > 0.0 && ++ticks >= CLOCK_EVERY) {
            ticks = 0;
            if (mg_time_now() >= deadline) {
                return;
            }
        }
        if (s->heap_n <= 0) {
            s->phase = NAV_PHASE_FAILED;
            return;
        }
        int32_t cur = heap_pop(m, &s->heap_n);
        if (m->stamp[cur] == s->closed_mark) {
            continue; /* stale heap entry */
        }
        m->stamp[cur] = s->closed_mark;
        if (cur == s->goal) {
            pull_begin(s, s->goal);
            break;
        }
        NavCell c = node_cell(g, cur);
        float h = octile(c.x, c.z, s->b.x, s->b.z);
        if (h < s->best_h) {
            s->best_h = h;
            s->best = cur;
        }
        for (int dz = -1; dz <= 1; dz++) {
            for (int dx = -1; dx <= 1; dx++) {
                if ((dx == 0 && dz == 0) || !walkable(g, c.x + dx, c.z + dz)) {
                    continue;
                }
                if (dx != 0 && dz != 0 && (!walkable(g, c.x + dx, c.z) || !walkable(g, c.x, c.z + dz))) {
                    continue; /* no corner cutting */
                }
                int next = g->base + (c.z + dz) * cols + c.x + dx;
                if (m->stamp[next] == s->closed_mark) {
                    continue;
                }
                float step = (dx != 0 && dz != 0) ? SQRT2 : 1.0f;
                if (g->flags[(c.z + dz) * cols + c.x + dx] & NAV_NEAR_WALL) {
                    step += 0.5f;
                }
                float cost = m->g[cur] + step;
                if (m->stamp[next] != s->open_mark || cost < m->g[next]) {
                    m->g[next] = cost;
                    m->parent[next] = cur;
                    m->stamp[next] = s->open_mark;
                    heap_push(m, &s->heap_n, cap, next, cost + k_h_weight * octile(c.x + dx, c.z + dz, s->b.x, s->b.z));
                }
            }
        }
    }
    while (s->phase == NAV_PHASE_PULL) {
        if (deadline > 0.0 && mg_time_now() >= deadline) {
            return;
        }
        pull_step(s);
    }
}

static float path_length(const NavPath *p)
{
    float sum = 0.0f;
    for (int i = 1; i < p->count; i++) {
        if (i != p->ride_at + 1) {
            HMM_Vec3 d = HMM_SubV3(p->pts[i].pos, p->pts[i - 1].pos);
            sum += sqrtf(d.X * d.X + d.Z * d.Z);
        }
    }
    return sum;
}

static void path_reset(NavPath *p)
{
    p->count = 0;
    p->ride_at = -1;
    p->ride_from = p->ride_to = 0;
    p->length = 0.0f;
}

/* start cell, target cell, and the car cells when the storeys differ */
static bool resolve(const NavWorld *n, HMM_Vec3 from, int gfrom, HMM_Vec3 to, int gto, NavCell *a, NavCell *b, NavCell *ca,
                    NavCell *cb)
{
    const NavGrid *ga = &n->grids[gfrom], *gb = &n->grids[gto];
    if (!nearest_walkable(ga, from.X, from.Z, a) || !nearest_walkable(gb, to.X, to.Z, b)) {
        return false;
    }
    if (gfrom != gto) {
        float cx = (n->car[0] + n->car[2]) * 0.5f, cz = (n->car[1] + n->car[3]) * 0.5f;
        return nearest_walkable(ga, cx, cz, ca) && nearest_walkable(gb, cx, cz, cb);
    }
    return true;
}

static uint16_t cell_label(const NavGrid *g, const uint16_t *labels, NavCell c) { return labels[c.z * g->d.cols + c.x]; }

static bool regions_connect(const NavWorld *n, int gfrom, int gto, NavCell a, NavCell b, NavCell ca, NavCell cb)
{
    const NavGrid *ga = &n->grids[gfrom], *gb = &n->grids[gto];
    if (gfrom == gto) {
        return cell_label(ga, ga->region, a) == cell_label(ga, ga->region, b);
    }
    return cell_label(ga, ga->region, a) == cell_label(ga, ga->region, ca) &&
           cell_label(gb, gb->region, cb) == cell_label(gb, gb->region, b);
}

bool nav_can_reach(const NavWorld *n, HMM_Vec3 from_pos, int from_storey, HMM_Vec3 to_pos, int to_storey)
{
    int gfrom = nav_grid_index(n, from_storey), gto = nav_grid_index(n, to_storey);
    NavCell a, b, ca = {0, 0}, cb = {0, 0};
    return gfrom >= 0 && gto >= 0 && resolve(n, from_pos, gfrom, to_pos, gto, &a, &b, &ca, &cb) &&
           regions_connect(n, gfrom, gto, a, b, ca, cb);
}

/* ------------------------------------------------------------- synchronous */

int nav_find_path(NavWorld *n, HMM_Vec3 from_pos, int from_storey, HMM_Vec3 to_pos, int to_storey, NavPath *path)
{
    double t0 = mg_time_now();
    path_reset(path);
    int gfrom = nav_grid_index(n, from_storey), gto = nav_grid_index(n, to_storey);
    int result = NAV_UNREACHABLE;
    NavCell a, b, ca = {0, 0}, cb = {0, 0};
    if (gfrom >= 0 && gto >= 0 && resolve(n, from_pos, gfrom, to_pos, gto, &a, &b, &ca, &cb)) {
        NavSearch s;
        if (gfrom == gto) {
            search_begin(&s, &n->mem[1], &n->grids[gfrom], a, b, gfrom, path, true);
            search_run(&s, 0.0);
            result = s.phase == NAV_PHASE_DONE ? 0 : NAV_UNREACHABLE;
        } else {
            search_begin(&s, &n->mem[1], &n->grids[gfrom], a, ca, gfrom, path, true);
            search_run(&s, 0.0);
            if (s.phase == NAV_PHASE_DONE) {
                path->ride_at = path->count - 1;
                path->ride_from = gfrom;
                path->ride_to = gto;
                add_point(path, &n->grids[gto], cb, gto);
                search_begin(&s, &n->mem[1], &n->grids[gto], cb, b, gto, path, false);
                search_run(&s, 0.0);
                result = s.phase == NAV_PHASE_DONE ? 0 : NAV_UNREACHABLE;
            }
        }
    }
    if (result == 0) {
        path->length = path_length(path);
    } else {
        path_reset(path);
    }
    n->path_ms = (float)((mg_time_now() - t0) * 1000.0);
    return result;
}

/* ------------------------------------------------------------------- cache */

void nav_cache_clear(NavWorld *n) { memset(n->cache, 0, sizeof(NavCacheEntry) * NAV_CACHE_SIZE); }

/* A cached room-to-room path, re-attached to the real start and end when
 * both are in sight of its first and last inner points. */
static bool cache_lookup(NavWorld *n, NavRequest *r)
{
    const NavGrid *ga = &n->grids[r->gfrom], *gb = &n->grids[r->gto];
    uint16_t ra = cell_label(ga, ga->room, r->a), rb = cell_label(gb, gb->room, r->b);
    if (!ra || !rb || (r->gfrom == r->gto && ra == rb)) {
        return false;
    }
    for (int i = 0; i < NAV_CACHE_SIZE; i++) {
        NavCacheEntry *e = &n->cache[i];
        if (!e->used || e->from_room != ra || e->to_room != rb || e->gfrom != r->gfrom || e->gto != r->gto ||
            e->path.count < 3) {
            continue;
        }
        const NavPoint *second = &e->path.pts[1], *penult = &e->path.pts[e->path.count - 2];
        if (e->path.ride_at == 0 || e->path.ride_at >= e->path.count - 2 || !line_clear(ga, r->a, cell_at(ga, second->pos.X, second->pos.Z)) ||
            !line_clear(gb, cell_at(gb, penult->pos.X, penult->pos.Z), r->b)) {
            continue;
        }
        r->path = e->path;
        r->path.pts[0] = point_at(ga, r->a, r->gfrom);
        r->path.pts[r->path.count - 1] = point_at(gb, r->b, r->gto);
        r->path.length = path_length(&r->path);
        e->stamp = ++n->cache_clock;
        return true;
    }
    return false;
}

static void cache_store(NavWorld *n, const NavRequest *r)
{
    const NavGrid *ga = &n->grids[r->gfrom], *gb = &n->grids[r->gto];
    uint16_t ra = cell_label(ga, ga->room, r->a), rb = cell_label(gb, gb->room, r->b);
    if (!ra || !rb || (r->gfrom == r->gto && ra == rb) || r->path.count < 3) {
        return;
    }
    NavCacheEntry *slot = &n->cache[0];
    for (int i = 0; i < NAV_CACHE_SIZE; i++) {
        NavCacheEntry *e = &n->cache[i];
        if (!e->used) {
            slot = e;
            break;
        }
        if (e->stamp < slot->stamp) {
            slot = e;
        }
    }
    slot->used = true;
    slot->from_room = ra;
    slot->to_room = rb;
    slot->gfrom = (int8_t)r->gfrom;
    slot->gto = (int8_t)r->gto;
    slot->stamp = ++n->cache_clock;
    slot->path = r->path;
}

/* ------------------------------------------------------------ the queue */

uint32_t nav_request(NavWorld *n, HMM_Vec3 from_pos, int from_storey, HMM_Vec3 to_pos, int to_storey, bool priority)
{
    NavRequest *r = NULL;
    for (int i = 0; i < NAV_MAX_REQUESTS && !r; i++) {
        r = n->reqs[i].state == NAV_REQ_FREE ? &n->reqs[i] : NULL;
    }
    if (!r) {
        return 0;
    }
    memset(r, 0, offsetof(NavRequest, s));
    r->cpu_ms = 0.0f;
    r->id = n->next_req++;
    r->seq = r->id;
    r->priority = priority;
    r->from_storey = from_storey;
    r->to_storey = to_storey;
    r->gfrom = nav_grid_index(n, from_storey);
    r->gto = nav_grid_index(n, to_storey);
    path_reset(&r->path);
    path_reset(&r->partial);
    r->cross = r->gfrom != r->gto;
    r->state = NAV_REQ_UNREACHABLE;
    if (r->gfrom >= 0 && r->gto >= 0 && resolve(n, from_pos, r->gfrom, to_pos, r->gto, &r->a, &r->b, &r->ca, &r->cb) &&
        regions_connect(n, r->gfrom, r->gto, r->a, r->b, r->ca, r->cb)) {
        if (cache_lookup(n, r)) {
            n->stats.cache_hits++;
            r->state = NAV_REQ_READY;
        } else {
            n->stats.cache_misses++;
            r->state = NAV_REQ_QUEUED;
        }
    }
    return r->id;
}

static void request_start_segment(NavWorld *n, NavRequest *r)
{
    if (!r->cross) {
        search_begin(&r->s, &n->mem[0], &n->grids[r->gfrom], r->a, r->b, r->gfrom, &r->path, true);
    } else if (r->segment == 0) {
        search_begin(&r->s, &n->mem[0], &n->grids[r->gfrom], r->a, r->ca, r->gfrom, &r->path, true);
    } else {
        search_begin(&r->s, &n->mem[0], &n->grids[r->gto], r->cb, r->b, r->gto, &r->path, false);
    }
}

/* After a slice that did not finish: a path towards the best cell so far. */
static void publish_partial(NavRequest *r)
{
    NavSearch *s = &r->s;
    if (r->has_partial || s->phase != NAV_PHASE_SEARCH || r->segment != 0 || s->best == -1) {
        return;
    }
    NavPath *p = &r->partial;
    path_reset(p);
    /* the live search keeps its heap: walk the parents without touching it */
    int cells[NAV_MAX_POINTS * 4], len = 0;
    for (int c = s->best; c != -1 && len < (int)(sizeof(cells) / sizeof(cells[0])); c = s->m->parent[c]) {
        cells[len++] = c;
    }
    if (len == (int)(sizeof(cells) / sizeof(cells[0]))) {
        return; /* longer than the buffer: wait for the whole path */
    }
    const NavGrid *g = s->g;
    NavCell anchor = s->a;
    add_point(p, g, anchor, s->storey);
    for (int i = len - 1; i > 0;) {
        int j = i - 1;
        while (j > 0 && line_clear(g, anchor, node_cell(g, cells[j - 1]))) {
            j--;
        }
        anchor = node_cell(g, cells[j]);
        add_point(p, g, anchor, s->storey);
        break; /* the first waypoint is all the walker needs until the real path lands */
    }
    r->has_partial = p->count >= 2;
}

static NavRequest *next_request(NavWorld *n)
{
    NavRequest *best = NULL;
    for (int i = 0; i < NAV_MAX_REQUESTS; i++) {
        NavRequest *r = &n->reqs[i];
        if (r->state == NAV_REQ_QUEUED && (!best || (r->priority && !best->priority) ||
                                           (r->priority == best->priority && r->seq < best->seq))) {
            best = r;
        }
    }
    return best;
}

void nav_service(NavWorld *n, double budget_ms)
{
    const double t0 = mg_time_now(), deadline = t0 + budget_ms * 0.001;
    while (mg_time_now() < deadline) {
        NavRequest *r = n->running >= 0 ? &n->reqs[n->running] : NULL;
        if (!r) {
            r = next_request(n);
            if (!r) {
                break;
            }
            n->running = (int)(r - n->reqs);
            r->state = NAV_REQ_RUNNING;
            r->segment = 0;
            request_start_segment(n, r);
        }
        const double slice0 = mg_time_now();
        search_run(&r->s, deadline);
        if (r->s.phase == NAV_PHASE_SEARCH || r->s.phase == NAV_PHASE_PULL) {
            publish_partial(r);
            r->cpu_ms += (float)((mg_time_now() - slice0) * 1000.0);
            break; /* out of budget: carry on next frame */
        }
        r->cpu_ms += (float)((mg_time_now() - slice0) * 1000.0);
        if (r->s.phase == NAV_PHASE_FAILED) {
            r->state = NAV_REQ_UNREACHABLE;
            path_reset(&r->path);
            n->running = -1;
        } else if (r->cross && r->segment == 0) {
            r->path.ride_at = r->path.count - 1;
            r->path.ride_from = r->gfrom;
            r->path.ride_to = r->gto;
            add_point(&r->path, &n->grids[r->gto], r->cb, r->gto);
            r->segment = 1;
            request_start_segment(n, r);
        } else {
            r->path.length = path_length(&r->path);
            r->state = NAV_REQ_READY;
            n->running = -1;
            cache_store(n, r);
            n->stats.longest_ms = r->cpu_ms > n->stats.longest_ms ? r->cpu_ms : n->stats.longest_ms;
            n->path_ms = r->cpu_ms;
        }
    }
    float spent = (float)((mg_time_now() - t0) * 1000.0);
    n->stats.worst_frame_ms = spent > n->stats.worst_frame_ms ? spent : n->stats.worst_frame_ms;
}

static NavRequest *find_request(NavWorld *n, uint32_t id)
{
    for (int i = 0; id && i < NAV_MAX_REQUESTS; i++) {
        if (n->reqs[i].id == id && n->reqs[i].state != NAV_REQ_FREE) {
            return &n->reqs[i];
        }
    }
    return NULL;
}

NavStatus nav_poll(NavWorld *n, uint32_t id, NavPath *path, bool *partial)
{
    if (partial) {
        *partial = false;
    }
    NavRequest *r = find_request(n, id);
    if (!r) {
        return NAV_STATUS_NONE;
    }
    if (r->state == NAV_REQ_READY) {
        if (path) {
            *path = r->path;
        }
        r->state = NAV_REQ_FREE;
        return NAV_STATUS_READY;
    }
    if (r->state == NAV_REQ_UNREACHABLE) {
        r->state = NAV_REQ_FREE;
        return NAV_STATUS_UNREACHABLE;
    }
    if (r->has_partial && !r->partial_taken && path && partial) {
        *path = r->partial;
        *partial = true;
        r->partial_taken = true;
    }
    return NAV_STATUS_PENDING;
}

void nav_cancel(NavWorld *n, uint32_t id)
{
    NavRequest *r = find_request(n, id);
    if (r) {
        if (n->running == (int)(r - n->reqs)) {
            n->running = -1;
        }
        r->state = NAV_REQ_FREE;
    }
}

NavStats nav_stats(const NavWorld *n) { return n->stats; }

void nav_stats_reset(NavWorld *n) { memset(&n->stats, 0, sizeof(n->stats)); }
