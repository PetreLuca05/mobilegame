#include <math.h>
#include <string.h>

#include "core/time.h"
#include "nav_internal.h"

/* per-grid scratch while baking */
typedef struct Cell {
    float top;    /* highest supporting surface near the floor */
    bool floor, blocked;
} Cell;

static float rect_dist(float x, float z, float x0, float z0, float x1, float z1)
{
    float dx = x < x0 ? x0 - x : (x > x1 ? x - x1 : 0.0f);
    float dz = z < z0 ? z0 - z : (z > z1 ? z - z1 : 0.0f);
    return sqrtf(dx * dx + dz * dz);
}

/* Every box touches only the cells under it (+ the capsule radius). */
static void raster_box(const NavGridDesc *d, Cell *cells, const ColBox *b, const NavBakeDesc *nb)
{
    const float y = d->floor_y;
    const float top = b->aabb_max.Y, bottom = b->aabb_min.Y;
    bool support = fabsf(top - y) <= nb->step + 0.01f;
    bool blocks = bottom < y + nb->height && top > y + nb->step;
    if (!support && !blocks) {
        return;
    }
    const float r = nb->radius;
    int cx0 = (int)floorf((b->aabb_min.X - r - d->x0) / NAV_CELL), cx1 = (int)floorf((b->aabb_max.X + r - d->x0) / NAV_CELL);
    int cz0 = (int)floorf((b->aabb_min.Z - r - d->z0) / NAV_CELL), cz1 = (int)floorf((b->aabb_max.Z + r - d->z0) / NAV_CELL);
    cx0 = cx0 < 0 ? 0 : cx0;
    cz0 = cz0 < 0 ? 0 : cz0;
    cx1 = cx1 >= d->cols ? d->cols - 1 : cx1;
    cz1 = cz1 >= d->rows ? d->rows - 1 : cz1;
    for (int z = cz0; z <= cz1; z++) {
        for (int x = cx0; x <= cx1; x++) {
            float wx = d->x0 + ((float)x + 0.5f) * NAV_CELL, wz = d->z0 + ((float)z + 0.5f) * NAV_CELL;
            float dist = rect_dist(wx, wz, b->aabb_min.X, b->aabb_min.Z, b->aabb_max.X, b->aabb_max.Z);
            Cell *c = &cells[z * d->cols + x];
            if (support && dist <= 0.0f && (!c->floor || top > c->top)) {
                c->floor = true;
                c->top = top;
            }
            if (blocks && dist < r) {
                c->blocked = true;
            }
        }
    }
}

static void bake_grid(NavGrid *g, Cell *cells, const ColWorld *world, const NavBakeDesc *nb)
{
    const NavGridDesc *d = &g->d;
    memset(cells, 0, sizeof(Cell) * (size_t)(d->cols * d->rows));
    for (int i = 0; i < world->count; i++) {
        raster_box(d, cells, &world->boxes[i], nb);
    }
    static const int nx[4] = {1, -1, 0, 0}, nz[4] = {0, 0, 1, -1};
    for (int z = 0; z < d->rows; z++) {
        for (int x = 0; x < d->cols; x++) {
            const Cell *c = &cells[z * d->cols + x];
            uint8_t f = 0;
            if (c->floor && !c->blocked) {
                f = NAV_WALKABLE;
                for (int k = 0; k < 4 && f; k++) { /* kerbs and thresholds only */
                    int ax = x + nx[k], az = z + nz[k];
                    if (ax < 0 || az < 0 || ax >= d->cols || az >= d->rows) {
                        continue;
                    }
                    const Cell *a = &cells[az * d->cols + ax];
                    if (a->floor && fabsf(a->top - c->top) > nb->step) {
                        f = 0;
                    }
                }
            }
            float wx = d->x0 + ((float)x + 0.5f) * NAV_CELL, wz = d->z0 + ((float)z + 0.5f) * NAV_CELL;
            if (f) {
                if (wx < nb->footprint[0] || wx > nb->footprint[2] || wz < nb->footprint[1] || wz > nb->footprint[3]) {
                    f |= NAV_OUTDOOR;
                }
                if (wx >= nb->car[0] && wx <= nb->car[2] && wz >= nb->car[1] && wz <= nb->car[3]) {
                    f |= NAV_ELEVATOR;
                }
            }
            g->flags[z * d->cols + x] = f;
        }
    }
    /* second pass: wall-hugging cells and narrow passages */
    for (int z = 0; z < d->rows; z++) {
        for (int x = 0; x < d->cols; x++) {
            uint8_t *f = &g->flags[z * d->cols + x];
            if (!(*f & NAV_WALKABLE)) {
                continue;
            }
            bool near = false;
            for (int dz = -1; dz <= 1 && !near; dz++) {
                for (int dx = -1; dx <= 1 && !near; dx++) {
                    int ax = x + dx, az = z + dz;
                    near = ax < 0 || az < 0 || ax >= d->cols || az >= d->rows ||
                           !(g->flags[az * d->cols + ax] & NAV_WALKABLE);
                }
            }
            if (near) {
                *f |= NAV_NEAR_WALL;
            }
            /* blocked within 3 cells on both sides along one axis */
            bool sides[4] = {false, false, false, false};
            for (int k = 1; k <= 3; k++) {
                for (int s = 0; s < 4; s++) {
                    int ax = x + nx[s] * k, az = z + nz[s] * k;
                    sides[s] = sides[s] || ax < 0 || az < 0 || ax >= d->cols || az >= d->rows ||
                               !(g->flags[az * d->cols + ax] & NAV_WALKABLE);
                }
            }
            if ((sides[0] && sides[1]) || (sides[2] && sides[3])) {
                *f |= NAV_DOOR;
            }
        }
    }
}

/* Flood fill of the walkable cells without `barrier` flags. Regions: an
 * impossible request is answered without searching. Rooms (doorways as the
 * barrier): the key of the path cache. */
static void label_cells(const NavGrid *g, uint16_t *label, uint8_t barrier, int32_t *stack)
{
    const int cols = g->d.cols, rows = g->d.rows;
    memset(label, 0, sizeof(uint16_t) * (size_t)(cols * rows));
    uint16_t next = 0;
    for (int start = 0; start < cols * rows; start++) {
        if (!(g->flags[start] & NAV_WALKABLE) || (g->flags[start] & barrier) || label[start]) {
            continue;
        }
        next++;
        int sp = 0;
        stack[sp++] = start;
        label[start] = next;
        while (sp > 0) {
            int c = stack[--sp], z = c / cols, x = c - z * cols;
            for (int dz = -1; dz <= 1; dz++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int ax = x + dx, az = z + dz;
                    if (ax < 0 || az < 0 || ax >= cols || az >= rows) {
                        continue;
                    }
                    int a = az * cols + ax;
                    if ((g->flags[a] & NAV_WALKABLE) && !(g->flags[a] & barrier) && !label[a]) {
                        label[a] = next;
                        stack[sp++] = a;
                    }
                }
            }
        }
    }
}

NavWorld *nav_bake(Arena *perm, const ColWorld *world, const NavBakeDesc *desc)
{
    double t0 = mg_time_now();
    NavWorld *n = ARENA_PUSH(perm, NavWorld, 1);
    if (!n) {
        return NULL;
    }
    memset(n, 0, sizeof(*n));
    n->grid_count = desc->grid_count;
    memcpy(n->car, desc->car, sizeof(n->car));
    int largest = 0;
    for (int i = 0; i < desc->grid_count; i++) {
        NavGrid *g = &n->grids[i];
        g->d = desc->grids[i];
        g->base = n->total;
        int cells = g->d.cols * g->d.rows;
        n->total += cells;
        largest = cells > largest ? cells : largest;
        g->flags = ARENA_PUSH(perm, uint8_t, (size_t)cells);
        g->region = ARENA_PUSH(perm, uint16_t, (size_t)cells);
        g->room = ARENA_PUSH(perm, uint16_t, (size_t)cells);
        if (!g->flags || !g->region || !g->room) {
            return NULL;
        }
    }
    for (int m = 0; m < 2; m++) { /* [0] the time-sliced queue, [1] synchronous nav_find_path */
        NavSearchMem *sm = &n->mem[m];
        sm->g = ARENA_PUSH(perm, float, (size_t)n->total);
        sm->parent = ARENA_PUSH(perm, int32_t, (size_t)n->total);
        sm->stamp = ARENA_PUSH(perm, uint32_t, (size_t)n->total);
        sm->heap = ARENA_PUSH(perm, int32_t, (size_t)n->total * 4);
        sm->heap_f = ARENA_PUSH(perm, float, (size_t)n->total * 4);
        if (!sm->g || !sm->parent || !sm->stamp || !sm->heap || !sm->heap_f) {
            return NULL;
        }
        memset(sm->stamp, 0, sizeof(uint32_t) * (size_t)n->total);
    }
    n->reqs = ARENA_PUSH(perm, NavRequest, NAV_MAX_REQUESTS);
    n->cache = ARENA_PUSH(perm, NavCacheEntry, NAV_CACHE_SIZE);
    if (!n->reqs || !n->cache) {
        return NULL;
    }
    memset(n->reqs, 0, sizeof(NavRequest) * NAV_MAX_REQUESTS);
    memset(n->cache, 0, sizeof(NavCacheEntry) * NAV_CACHE_SIZE);
    n->next_req = 1;
    n->running = -1;
    /* labels below use the search heap as their flood-fill stack */
    size_t mark = arena_mark(perm);
    Cell *cells = ARENA_PUSH(perm, Cell, (size_t)largest);
    if (!cells) {
        return NULL;
    }
    for (int i = 0; i < desc->grid_count; i++) {
        bake_grid(&n->grids[i], cells, world, desc);
        label_cells(&n->grids[i], n->grids[i].region, 0, n->mem[0].heap);
        label_cells(&n->grids[i], n->grids[i].room, NAV_DOOR, n->mem[0].heap);
    }
    arena_reset_to(perm, mark);
    n->bake_ms = (float)((mg_time_now() - t0) * 1000.0);
    return n;
}

const NavGrid *nav_grid(const NavWorld *n, int index) { return &n->grids[index]; }
int nav_grid_count(const NavWorld *n) { return n->grid_count; }
float nav_bake_ms(const NavWorld *n) { return n->bake_ms; }
float nav_path_ms(const NavWorld *n) { return n->path_ms; }

uint8_t nav_flags_at(const NavWorld *n, int storey, float x, float z)
{
    int gi = nav_grid_index(n, storey);
    if (gi < 0) {
        return 0;
    }
    const NavGrid *g = &n->grids[gi];
    int cx = (int)floorf((x - g->d.x0) / NAV_CELL), cz = (int)floorf((z - g->d.z0) / NAV_CELL);
    return cx < 0 || cz < 0 || cx >= g->d.cols || cz >= g->d.rows ? 0 : g->flags[cz * g->d.cols + cx];
}
