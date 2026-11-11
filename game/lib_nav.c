/* LIB-1 §7: the library hall's walkability grid, paths and walking agents.
 * Ported from the browser prototype (source of truth): bake, navNear, navLOS,
 * navPath0, navPath, steerStep, Av.walkTo / follow. Differences: the open set
 * is an indexed binary heap (same costs, ties may break differently), stamping
 * touches only each disc's bounding cells, and paths come through a queue. */
#include "lib_nav.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "core/log.h"
#include "core/time.h"

#define SQRT2_COST 1.4142f /* the prototype's diagonal step */
#define H_DIAG 0.4142f

/* ------------------------------------------------------------------ bake */

static float jnum(const cJSON *o, const char *k, float def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : def;
}

int lib_nav_bay_of(const LibNav *n, float x, float z)
{
    int c = (int)floorf((x - n->x_min) / n->pitch);
    c = c < 0 ? 0 : (c > n->cols - 1 ? n->cols - 1 : c);
    return (z < n->mid_wall_z ? n->cols : 0) + c;
}

bool lib_nav_bake(LibNav *n, const cJSON *root)
{
    double t0 = mg_time_now();
    memset(n, 0, sizeof *n);
    n->scr = &n->scratch;
    const cJSON *nav = cJSON_GetObjectItemCaseSensitive(root, "nav");
    const cJSON *hall = cJSON_GetObjectItemCaseSensitive(root, "hall");
    const cJSON *obst = cJSON_GetObjectItemCaseSensitive(root, "obstacles");
    const cJSON *dx = cJSON_GetObjectItemCaseSensitive(nav, "door_x");
    const cJSON *bx = cJSON_GetObjectItemCaseSensitive(hall, "bx");
    const cJSON *bz = cJSON_GetObjectItemCaseSensitive(hall, "bz");
    if (!cJSON_IsObject(nav) || !cJSON_IsObject(hall) || !cJSON_IsArray(obst) ||
        cJSON_GetArraySize(dx) != 2 || !cJSON_IsArray(bx) || cJSON_GetArraySize(bz) != cJSON_GetArraySize(bx)) {
        mg_log_error("lib_nav: scene lacks nav / hall (bx, bz) / obstacles (mg-library 10)");
        return false;
    }
    n->cell = jnum(nav, "cell", 0.25f);
    n->x0 = jnum(nav, "x0", 0);
    n->z0 = jnum(nav, "z0", 0);
    n->nx = (int)jnum(nav, "nx", 0);
    n->nz = (int)jnum(nav, "nz", 0);
    n->radius = jnum(nav, "radius", 0.3f);
    n->door_x[0] = (float)cJSON_GetArrayItem(dx, 0)->valuedouble;
    n->door_x[1] = (float)cJSON_GetArrayItem(dx, 1)->valuedouble;
    n->wall_t = jnum(nav, "wall_t", 0.2f);
    n->x_min = jnum(hall, "x_min", 0);
    n->x_max = jnum(hall, "x_max", 0);
    n->z_min = jnum(hall, "z_min", 0);
    n->z_max = jnum(hall, "z_max", 0);
    n->mid_wall_z = jnum(hall, "mid_wall_z", 0);
    n->pitch = jnum(hall, "pitch", 16);
    n->bays = cJSON_GetArraySize(bx);
    n->cols = (int)jnum(hall, "cols", (float)n->bays);
    if (n->nx <= 0 || n->nz <= 0 || n->nx > LIB_NAV_NX_MAX || n->nz > LIB_NAV_NZ_MAX || n->nx * n->nz > LIB_NAV_CELLS ||
        n->bays < 1 || n->bays > LIB_NAV_BAYS_MAX || n->cols < 1 || n->cols > n->bays || n->cell <= 0 || n->pitch <= 0) {
        mg_log_error("lib_nav: grid %dx%d / %d bays in %d columns out of range", n->nx, n->nz, n->bays, n->cols);
        return false;
    }
    for (int i = 0; i < n->bays; i++) {
        n->bx[i] = (float)cJSON_GetArrayItem(bx, i)->valuedouble;
        n->bz[i] = (float)cJSON_GetArrayItem(bz, i)->valuedouble;
    }

    const float r = n->radius;
    /* walls: behind the back row only a bay's door corridor; the hall's sides and front; the wall between the rows,
     * open at the front row's doors */
    const float mw = n->mid_wall_z;
    for (int j = 0; j < n->nz; j++) {
        float z = n->z0 + ((float)j + 0.5f) * n->cell;
        for (int i = 0; i < n->nx; i++) {
            float x = n->x0 + ((float)i + 0.5f) * n->cell;
            float lx = x - n->bx[lib_nav_bay_of(n, x, z)];
            bool dr = lx > n->door_x[0] && lx < n->door_x[1];
            uint8_t b = 0;
            if (z < n->z_min + r) b = dr ? 0 : 1;
            else if (x < n->x_min + r || x > n->x_max - r || z > n->z_max - r) b = 1;
            else if (z > mw - n->wall_t - r && z < mw + r) b = dr ? 0 : 1;
            n->cells[j * n->nx + i] = b;
        }
    }
    /* obstacles grown by the radius: strict inside test on cell centres, rasterised over the covered cells */
    int nob = 0;
    const cJSON *o;
    cJSON_ArrayForEach(o, obst)
    {
        if (cJSON_GetArraySize(o) != 4) continue;
        float ox0 = (float)cJSON_GetArrayItem(o, 0)->valuedouble - r, oz0 = (float)cJSON_GetArrayItem(o, 1)->valuedouble - r;
        float ox1 = (float)cJSON_GetArrayItem(o, 2)->valuedouble + r, oz1 = (float)cJSON_GetArrayItem(o, 3)->valuedouble + r;
        int i0 = (int)floorf((ox0 - n->x0) / n->cell - 0.5f), i1 = (int)ceilf((ox1 - n->x0) / n->cell - 0.5f);
        int j0 = (int)floorf((oz0 - n->z0) / n->cell - 0.5f), j1 = (int)ceilf((oz1 - n->z0) / n->cell - 0.5f);
        if (i0 < 0) i0 = 0;
        if (j0 < 0) j0 = 0;
        if (i1 > n->nx - 1) i1 = n->nx - 1;
        if (j1 > n->nz - 1) j1 = n->nz - 1;
        for (int j = j0; j <= j1; j++) {
            float z = n->z0 + ((float)j + 0.5f) * n->cell;
            if (!(z > oz0 && z < oz1)) continue;
            for (int i = i0; i <= i1; i++) {
                float x = n->x0 + ((float)i + 0.5f) * n->cell;
                if (x > ox0 && x < ox1) n->cells[j * n->nx + i] = 1;
            }
        }
        nob++;
    }
    for (int c = 0; c < n->nx * n->nz; c++) n->free_count += n->cells[c] == 0;
    n->bake_ms = (mg_time_now() - t0) * 1000.0;
    mg_log_info("lib_nav: baked %dx%d cells, %d obstacles, %d free, %.3f ms", n->nx, n->nz, nob, n->free_count,
                n->bake_ms);
    return true;
}

/* ------------------------------------------------------------------ queries */

int lib_nav_cell(const LibNav *n, HMM_Vec2 p)
{
    int i = (int)floorf((p.X - n->x0) / n->cell), j = (int)floorf((p.Y - n->z0) / n->cell);
    return (i < 0 || j < 0 || i >= n->nx || j >= n->nz) ? -1 : j * n->nx + i;
}

bool lib_nav_free(const LibNav *n, HMM_Vec2 p)
{
    int c = lib_nav_cell(n, p);
    return c >= 0 && !n->cells[c];
}

HMM_Vec2 lib_nav_cell_xz(const LibNav *n, int c)
{
    return HMM_V2(n->x0 + ((float)(c % n->nx) + 0.5f) * n->cell, n->z0 + ((float)(c / n->nx) + 0.5f) * n->cell);
}

/* navNear: the free cell nearest p. The prototype scans every cell; rings out from p's cell give the same answer:
 * a ring at Chebyshev distance k holds no cell closer than (k - 1) cells, so stop once that exceeds the best. */
int lib_nav_near(const LibNav *n, HMM_Vec2 p)
{
    int c = lib_nav_cell(n, p);
    if (c >= 0 && !n->cells[c]) return c;
    int ci = (int)floorf((p.X - n->x0) / n->cell), cj = (int)floorf((p.Y - n->z0) / n->cell);
    int best = -1;
    float bd = 1e30f;
    int kmax = n->nx + n->nz + abs(ci) + abs(cj);
    for (int k = 0; k <= kmax; k++) {
        float lim = ((float)k - 1.0f) * n->cell;
        if (best >= 0 && lim > 0 && lim * lim > bd) break;
        for (int j = cj - k; j <= cj + k; j++) {
            if (j < 0 || j >= n->nz) continue;
            bool edge = j == cj - k || j == cj + k;
            for (int i = ci - k; i <= ci + k; i += (edge || k == 0) ? 1 : 2 * k) {
                if (i < 0 || i >= n->nx) continue;
                int q = j * n->nx + i;
                if (n->cells[q]) continue;
                HMM_Vec2 xz = lib_nav_cell_xz(n, q);
                float d = (xz.X - p.X) * (xz.X - p.X) + (xz.Y - p.Y) * (xz.Y - p.Y);
                if (d < bd || (d == bd && q < best)) {
                    bd = d;
                    best = q;
                }
            }
        }
    }
    return best;
}

static bool cell_open(const LibNav *n, int i, int j)
{
    return i >= 0 && j >= 0 && i < n->nx && j < n->nz && !n->cells[j * n->nx + i];
}

/* navLOS samples every 0.1 m, which can miss a corner a walker's 2 cm steps then clip (it stops there). This walks
 * every cell the segment touches instead (a corner touch checks both side cells, like "no corner cutting"); the
 * start and end cells are left out, as the prototype leaves out its end samples. */
bool lib_nav_los(const LibNav *n, HMM_Vec2 a, HMM_Vec2 b)
{
    float fx = (a.X - n->x0) / n->cell, fz = (a.Y - n->z0) / n->cell;
    float gx = (b.X - n->x0) / n->cell, gz = (b.Y - n->z0) / n->cell;
    int i = (int)floorf(fx), j = (int)floorf(fz), ie = (int)floorf(gx), je = (int)floorf(gz);
    float dx = gx - fx, dz = gz - fz;
    int si = dx > 0 ? 1 : -1, sj = dz > 0 ? 1 : -1;
    float tdx = dx != 0 ? 1.0f / fabsf(dx) : 1e30f, tdz = dz != 0 ? 1.0f / fabsf(dz) : 1e30f;
    float tx = dx != 0 ? (si > 0 ? ((float)i + 1.0f - fx) : (fx - (float)i)) * tdx : 1e30f;
    float tz = dz != 0 ? (sj > 0 ? ((float)j + 1.0f - fz) : (fz - (float)j)) * tdz : 1e30f;
    int guard = abs(ie - i) + abs(je - j) + 2;
    while ((i != ie || j != je) && guard-- > 0) {
        const float eps = 1e-5f;
        if (tx < tz - eps) {
            i += si;
            tx += tdx;
        } else if (tz < tx - eps) {
            j += sj;
            tz += tdz;
        } else { /* through a corner */
            if ((i + si != ie || j != je) && !cell_open(n, i + si, j)) return false;
            if ((i != ie || j + sj != je) && !cell_open(n, i, j + sj)) return false;
            i += si;
            j += sj;
            tx += tdx;
            tz += tdz;
        }
        if ((i != ie || j != je) && !cell_open(n, i, j)) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ A* */

static void heap_swap(LibNavScratch *s, int a, int b)
{
    int16_t t = s->heap[a];
    s->heap[a] = s->heap[b];
    s->heap[b] = t;
    s->hpos[s->heap[a]] = (int16_t)a;
    s->hpos[s->heap[b]] = (int16_t)b;
}

static void heap_up(LibNavScratch *s, int i)
{
    while (i > 0) {
        int p = (i - 1) / 2;
        if (s->f[s->heap[p]] <= s->f[s->heap[i]]) break;
        heap_swap(s, i, p);
        i = p;
    }
}

static void heap_down(LibNavScratch *s, int i, int len)
{
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < len && s->f[s->heap[l]] < s->f[s->heap[m]]) m = l;
        if (r < len && s->f[s->heap[r]] < s->f[s->heap[m]]) m = r;
        if (m == i) return;
        heap_swap(s, i, m);
        i = m;
    }
}

static float h_cost(const LibNav *n, int c, HMM_Vec2 gp)
{
    HMM_Vec2 p = lib_nav_cell_xz(n, c);
    float dx = fabsf(p.X - gp.X), dz = fabsf(p.Y - gp.Y);
    return (dx > dz ? dx : dz) + H_DIAG * (dx < dz ? dx : dz);
}

static void path_to(LibPath *out, HMM_Vec2 to)
{
    out->pts[0] = to;
    out->n = 1;
}

/* navPath0. false = no route (the prototype's null). */
static bool path0(const LibNav *n, HMM_Vec2 from, HMM_Vec2 to, LibPath *out)
{
    LibNavScratch *S = n->scr;
    int s = lib_nav_near(n, from), g = lib_nav_near(n, to);
    if (s < 0 || g < 0) {
        path_to(out, to);
        return true;
    }
    if (++S->gen == 0) { /* wrapped: clear the stamps */
        memset(S->seen, 0, sizeof S->seen);
        memset(S->closed, 0, sizeof S->closed);
        S->gen = 1;
    }
    const uint16_t gen = S->gen;
    const int nx = n->nx, nz = n->nz;
    HMM_Vec2 gp = lib_nav_cell_xz(n, g);
    static const int8_t DI[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DJ[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    S->seen[s] = gen;
    S->g[s] = 0;
    S->f[s] = h_cost(n, s, gp);
    S->came[s] = -1;
    S->heap[0] = (int16_t)s;
    S->hpos[s] = 0;
    int len = 1;
    bool found = false;
    while (len > 0) {
        int c = S->heap[0];
        S->hpos[c] = -1;
        if (--len > 0) {
            S->heap[0] = S->heap[len];
            S->hpos[S->heap[0]] = 0;
            heap_down(S, 0, len);
        }
        if (c == g) {
            found = true;
            break;
        }
        S->closed[c] = gen;
        int ci = c % nx, cj = c / nx;
        for (int k = 0; k < 8; k++) {
            int di = DI[k], dj = DJ[k], ni = ci + di, nj = cj + dj;
            if (ni < 0 || nj < 0 || ni >= nx || nj >= nz) continue;
            int m = nj * nx + ni;
            if (n->cells[m] || S->closed[m] == gen) continue;
            if (di && dj && (n->cells[cj * nx + ni] || n->cells[nj * nx + ci])) continue;
            float ng = S->g[c] + (di && dj ? SQRT2_COST : 1.0f) * n->cell;
            if (S->seen[m] != gen) {
                S->seen[m] = gen;
                S->g[m] = 1e30f;
                S->hpos[m] = -1;
            }
            if (ng < S->g[m]) {
                S->g[m] = ng;
                S->f[m] = ng + h_cost(n, m, gp);
                S->came[m] = (int16_t)c;
                if (S->hpos[m] < 0) {
                    S->heap[len] = (int16_t)m;
                    S->hpos[m] = (int16_t)len;
                    len++;
                }
                heap_up(S, S->hpos[m]);
            }
        }
    }
    if (!found && g != s) return false;
    /* pts = [from, cells..., to], then string-pull */
    const int cap = (int)(sizeof S->pts / sizeof S->pts[0]);
    int cnt = 0;
    for (int c = g;; c = S->came[c]) {
        cnt++;
        if (c == s || S->came[c] < 0) break;
    }
    if (cnt + 2 > cap) return false;
    S->pts[0] = from;
    int k = cnt;
    for (int c = g;; c = S->came[c]) {
        S->pts[k--] = lib_nav_cell_xz(n, c);
        if (c == s || S->came[c] < 0) break;
    }
    S->pts[cnt + 1] = to;
    int np = cnt + 2, i = 0;
    out->n = 0;
    while (i < np - 1) {
        int j = np - 1;
        while (j > i + 1 && !lib_nav_los(n, S->pts[i], S->pts[j])) j--;
        if (out->n >= LIB_NAV_PATH_MAX) return false;
        out->pts[out->n++] = S->pts[j];
        i = j;
    }
    return true;
}

bool lib_nav_path(const LibNav *n, HMM_Vec2 from, HMM_Vec2 to, const HMM_Vec3 *discs, int ndiscs, LibPath *out)
{
    LibNavScratch *S = n->scr;
    uint8_t *cells = (uint8_t *)n->cells; /* stamps are undone before returning */
    int to_c = lib_nav_cell(n, to);
    out->last_straight = to_c < 0 || n->cells[to_c] != 0;
    bool ok = false;
    if (ndiscs > 0) {
        int nm = 0;
        const int mcap = (int)(sizeof S->marks / sizeof S->marks[0]);
        for (int d = 0; d < ndiscs; d++) {
            HMM_Vec3 q = discs[d];
            int i0 = (int)floorf((q.X - q.Z - n->x0) / n->cell), i1 = (int)floorf((q.X + q.Z - n->x0) / n->cell);
            int j0 = (int)floorf((q.Y - q.Z - n->z0) / n->cell), j1 = (int)floorf((q.Y + q.Z - n->z0) / n->cell);
            if (i0 < 0) i0 = 0;
            if (j0 < 0) j0 = 0;
            if (i1 > n->nx - 1) i1 = n->nx - 1;
            if (j1 > n->nz - 1) j1 = n->nz - 1;
            for (int j = j0; j <= j1; j++)
                for (int i = i0; i <= i1; i++) {
                    int c = j * n->nx + i;
                    if (cells[c] || nm >= mcap) continue;
                    HMM_Vec2 p = lib_nav_cell_xz(n, c);
                    float dq = sqrtf((p.X - q.X) * (p.X - q.X) + (p.Y - q.Y) * (p.Y - q.Y));
                    float df = sqrtf((p.X - from.X) * (p.X - from.X) + (p.Y - from.Y) * (p.Y - from.Y));
                    if (dq < q.Z && df > 0.3f) {
                        cells[c] = LIB_CELL_STAMP;
                        S->marks[nm++] = (int16_t)c;
                    }
                }
        }
        ok = path0(n, from, to, out);
        for (int m = 0; m < nm; m++) cells[S->marks[m]] = LIB_CELL_FREE;
    }
    if (!ok) ok = path0(n, from, to, out);
    if (!ok) path_to(out, to);
    return ok;
}

/* ------------------------------------------------------------------ queue */

void lib_nav_queue_init(LibNavQueue *q, const LibNav *n)
{
    memset(q, 0, sizeof *q);
    q->nav = n;
    q->next_ticket = 1;
}

static int slot_of(const LibNavQueue *q, uint32_t ticket)
{
    if (!ticket) return -1;
    for (int i = 0; i < LIB_NAV_QUEUE_MAX; i++)
        if (q->r[i].state != LIB_REQ_NONE && q->r[i].ticket == ticket) return i;
    return -1;
}

uint32_t lib_nav_request(LibNavQueue *q, HMM_Vec2 from, HMM_Vec2 to, const HMM_Vec3 *discs, int ndiscs)
{
    for (int i = 0; i < LIB_NAV_QUEUE_MAX; i++) {
        LibNavReq *r = &q->r[i];
        if (r->state != LIB_REQ_NONE) continue;
        r->state = LIB_REQ_PENDING;
        r->ticket = q->next_ticket++;
        if (!q->next_ticket) q->next_ticket = 1;
        r->seq = q->next_seq++;
        r->from = from;
        r->to = to;
        r->ndiscs = ndiscs < LIB_NAV_DISCS_MAX ? (ndiscs > 0 ? ndiscs : 0) : LIB_NAV_DISCS_MAX;
        for (int d = 0; d < r->ndiscs; d++) r->discs[d] = discs[d];
        return r->ticket;
    }
    return 0;
}

void lib_nav_service(LibNavQueue *q, double budget_ms)
{
    double t0 = mg_time_now();
    for (;;) {
        int best = -1;
        for (int i = 0; i < LIB_NAV_QUEUE_MAX; i++)
            if (q->r[i].state == LIB_REQ_PENDING && (best < 0 || q->r[i].seq - q->r[best].seq > 0x80000000u))
                best = i;
        if (best < 0) return;
        LibNavReq *r = &q->r[best];
        double s0 = mg_time_now();
        bool ok = lib_nav_path(q->nav, r->from, r->to, r->discs, r->ndiscs, &r->path);
        double now = mg_time_now(), ms = (now - s0) * 1000.0;
        if (ms > q->search_ms_max) q->search_ms_max = ms;
        q->searches++;
        r->state = ok ? LIB_REQ_READY : LIB_REQ_FAILED;
        if ((now - t0) * 1000.0 >= budget_ms) return;
    }
}

void lib_nav_service_n(LibNavQueue *q, int searches)
{
    for (int k = 0; k < searches; k++) {
        int best = -1;
        for (int i = 0; i < LIB_NAV_QUEUE_MAX; i++)
            if (q->r[i].state == LIB_REQ_PENDING && (best < 0 || q->r[i].seq - q->r[best].seq > 0x80000000u))
                best = i;
        if (best < 0) return;
        LibNavReq *r = &q->r[best];
        double s0 = mg_time_now();
        bool ok = lib_nav_path(q->nav, r->from, r->to, r->discs, r->ndiscs, &r->path);
        double ms = (mg_time_now() - s0) * 1000.0;
        if (ms > q->search_ms_max) q->search_ms_max = ms;
        q->searches++;
        r->state = ok ? LIB_REQ_READY : LIB_REQ_FAILED;
    }
}

LibReqState lib_nav_poll(LibNavQueue *q, uint32_t ticket, LibPath *out)
{
    int i = slot_of(q, ticket);
    if (i < 0) return LIB_REQ_NONE;
    LibReqState st = q->r[i].state;
    if (st == LIB_REQ_READY || st == LIB_REQ_FAILED) {
        if (out) *out = q->r[i].path;
        q->r[i].state = LIB_REQ_NONE;
    }
    return st;
}

void lib_nav_cancel(LibNavQueue *q, uint32_t ticket)
{
    int i = slot_of(q, ticket);
    if (i >= 0) q->r[i].state = LIB_REQ_NONE;
}

/* ------------------------------------------------------------------ agents */

static float dist2(HMM_Vec2 a, HMM_Vec2 b) { return sqrtf((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y)); }
static float ease(float dt, float rate) { return 1.0f - expf(-rate * dt); }
static float angle_lerp(float a, float b, float t)
{
    const float TAU = 6.28318531f;
    float d = fmodf(b - a + 3.14159265f, TAU);
    if (d < 0) d += TAU;
    return a + (d - 3.14159265f) * t;
}

void lib_agents_init(LibAgents *s, const LibNav *n)
{
    memset(s, 0, sizeof *s);
    lib_nav_queue_init(&s->q, n);
    s->service_budget_ms = 1.0;
}

int lib_agent_add(LibAgents *s, HMM_Vec2 pos, float radius, float speed)
{
    if (s->count >= LIB_AGENTS_MAX) return -1;
    int id = s->count++;
    LibAgent *a = &s->a[id];
    memset(a, 0, sizeof *a);
    a->pos = pos;
    a->radius = radius;
    a->speed = speed;
    a->active = true;
    a->standing_still = true;
    a->ignores_agent = -1;
    a->cart_of = -1;
    a->cart = -1;
    a->arrived = true;
    a->goal = pos;
    return id;
}

int lib_agent_add_cart(LibAgents *s, int pusher)
{
    LibAgent *p = &s->a[pusher];
    HMM_Vec2 at = HMM_V2(p->pos.X + sinf(p->yaw) * LIB_CART_AHEAD, p->pos.Y + cosf(p->yaw) * LIB_CART_AHEAD);
    int id = lib_agent_add(s, at, LIB_CART_RADIUS, 0);
    if (id < 0) return -1;
    s->a[id].is_cart = true;
    s->a[id].cart_of = pusher;
    s->a[id].yaw = p->yaw;
    p = &s->a[pusher];
    p->cart = id;
    p->ignores_agent = id;
    p->cart_pushed = true;
    return id;
}

void lib_agent_go(LibAgents *s, const LibNav *n, int id, HMM_Vec2 goal)
{
    LibAgent *a = &s->a[id];
    if (!s->q.nav) s->q.nav = n;
    if (a->ticket) lib_nav_cancel(&s->q, a->ticket);
    a->goal = goal;
    a->has_path = false;
    a->arrived = false;
    a->standing_still = true; /* until the path comes */
    a->next = 0;
    a->repathed = false;
    a->blocked_s = 0;
    a->ticket_is_repath = false;
    a->ticket = lib_nav_request(&s->q, a->pos, goal, NULL, 0);
}

void lib_agent_stop(LibAgents *s, int id)
{
    LibAgent *a = &s->a[id];
    if (a->ticket) lib_nav_cancel(&s->q, a->ticket);
    a->ticket = 0;
    a->has_path = false;
    a->arrived = true;
    a->standing_still = true;
    a->vel = HMM_V2(0, 0);
}

/* the agent index that owns snapshot entry k is "self" for a and for its cart */
static bool snap_clear(const LibAgents *s, int self, HMM_Vec2 p, float rr, HMM_Vec2 from)
{
    const float k = s->a[self].ignore_others_s > 0 ? LIB_GHOST_HARD_RULE : LIB_HARD_RULE;
    if (k <= 0) return true;
    for (int j = 0; j < s->nsnap; j++) {
        const LibAgentSnap *q = &s->snap[j];
        if (q->owner == self) continue;
        float nd = dist2(q->p, p);
        if (nd < (rr + q->r) * k && nd < dist2(q->p, from) + 1e-4f) return false;
    }
    return true;
}

/* the facing after a step from g to np (Av.walkTo turns at ease 8 /s towards the motion) */
static float step_yaw(const LibAgent *a, HMM_Vec2 g, HMM_Vec2 np, float dt)
{
    float mx = np.X - g.X, mz = np.Y - g.Y;
    return mx * mx + mz * mz > 1e-9f ? angle_lerp(a->yaw, atan2f(mx, mz), ease(dt, 8)) : a->yaw;
}

/* A pusher checks its cart's place. The prototype checks 0.75 m along the candidate direction while the cart then
 * follows the smoothed facing; checking the place the cart actually goes to (facing after this step) keeps the
 * hard rule true for the cart too. */
static bool cart_ok(const LibAgents *s, int self, HMM_Vec2 np, float yaw)
{
    const LibAgent *a = &s->a[self];
    if (a->cart < 0 || !a->cart_pushed) return true;
    HMM_Vec2 cp = HMM_V2(np.X + sinf(yaw) * LIB_CART_AHEAD, np.Y + cosf(yaw) * LIB_CART_AHEAD);
    return snap_clear(s, self, cp, LIB_CART_RADIUS, s->a[a->cart].pos);
}

/* steerStep: 1 reached p, 0 moved, -1 blocked */
static int steer_step(LibAgents *s, const LibNav *n, int self, HMM_Vec2 p, float dt, bool last)
{
    LibAgent *a = &s->a[self];
    const float r = a->radius;
    HMM_Vec2 g = a->pos;
    float dx = p.X - g.X, dz = p.Y - g.Y, d = sqrtf(dx * dx + dz * dz), step = a->speed * dt;
    if (a->ignore_others_s > 0) a->ignore_others_s -= dt;
    if (d <= step && snap_clear(s, self, p, r, g) && cart_ok(s, self, p, step_yaw(a, g, p, dt))) {
        a->cart_checked = true;
        a->yaw = step_yaw(a, g, p, dt);
        a->pos = p;
        a->blocked_s = 0;
        return 1;
    }
    const bool ghost = a->ignore_others_s > 0;
    if (!ghost && last && d < LIB_STOP_NEAR && !snap_clear(s, self, p, r, g)) return 1;
    float inv = d > 0 ? 1.0f / d : 1.0f;
    float ux = dx * inv, uz = dz * inv, sx = ux, sz = uz;
    for (int i = 0; i < s->nsnap && !ghost; i++) {
        const LibAgentSnap *b = &s->snap[i];
        if (b->owner == self) continue;
        float bx = b->p.X - g.X, bz = b->p.Y - g.Y, bd = sqrtf(bx * bx + bz * bz), R = r + b->r;
        if (bd < R + LIB_STEER_RANGE && bx * ux + bz * uz > -0.1f) {
            float w = 1.0f - (bd - R) / LIB_STEER_RANGE;
            if (w > 1) w = 1;
            float c = ux * bz - uz * bx, sg = c < -0.02f ? -1.0f : 1.0f;
            sx += sg * uz * w * LIB_STEER_WEIGHT;
            sz -= sg * ux * w * LIB_STEER_WEIGHT;
        }
    }
    float sl = sqrtf(sx * sx + sz * sz);
    if (sl == 0) sl = 1;
    const float cand[4][2] = {{sx / sl, sz / sl}, {ux, uz}, {uz, -ux}, {-uz, ux}};
    bool here = lib_nav_free(n, g);
    /* steered steps stay on free cells except on the last stretch to a goal in a blocked cell (the prototype lets
     * any final segment leave the grid; a walker could then be steered off it next to a free goal) */
    bool off_grid_ok = last && a->path.last_straight;
    if (step > d) step = d;
    /* the ghost spell also offers a step back at half speed, facing kept (a pusher reverses its cart): in a jam in a
     * narrow aisle the hard rule never gives way, so someone has to make room */
    /* a jam at a door: the hard rule stays at 96 % for everyone, so the spell also offers the two back diagonals */
    const float bd = 0.70710678f;
    const float back[3][2] = {{-ux, -uz}, {(-ux + uz) * bd, (-uz - ux) * bd}, {(-ux - uz) * bd, (-uz + ux) * bd}};
    /* ... and whoever stands blocked in front of someone in such a spell gives way the same way */
    bool yield = false;
    for (int i = 0; i < s->nsnap && !ghost && !yield && a->blocked_s > 0.5f; i++) {
        const LibAgentSnap *b = &s->snap[i];
        if (b->owner == self || !b->ghost) continue;
        float bx = b->p.X - g.X, bz = b->p.Y - g.Y;
        yield = sqrtf(bx * bx + bz * bz) < r + b->r + LIB_STEER_RANGE;
    }
    const int ncand = ghost || yield ? 7 : 4;
    for (int i = 0; i < ncand; i++) {
        float cx = i < 4 ? cand[i][0] : back[i - 4][0], cz = i < 4 ? cand[i][1] : back[i - 4][1], st = i < 4 ? step : 0.5f * step;
        HMM_Vec2 np = HMM_V2(g.X + cx * st, g.Y + cz * st);
        if (!(off_grid_ok || !here || lib_nav_free(n, np))) continue;
        if (!snap_clear(s, self, np, r, g)) continue;
        float ny = i < 4 ? step_yaw(a, g, np, dt) : a->yaw;
        if (!cart_ok(s, self, np, ny)) {
            /* the cart cannot swing round there: move it along without turning (it slides with the pusher) */
            ny = a->yaw;
            if (!cart_ok(s, self, np, ny)) continue;
        }
        a->yaw = ny;
        a->pos = np;
        a->cart_checked = true;
        if (i < 2) a->blocked_s = a->blocked_s - dt > 0 ? a->blocked_s - dt : 0;
        else a->blocked_s += dt * 0.6f;
        return 0;
    }
    a->blocked_s += dt;
    return -1;
}

static void request_repath(LibAgents *s, int self)
{
    LibAgent *a = &s->a[self];
    HMM_Vec3 discs[LIB_NAV_DISCS_MAX];
    int nd = 0;
    for (int i = 0; i < s->nsnap && nd < LIB_NAV_DISCS_MAX; i++) {
        const LibAgentSnap *q = &s->snap[i];
        if (q->owner == self || dist2(q->p, a->pos) > LIB_STAMP_RANGE) continue;
        discs[nd++] = HMM_V3(q->p.X, q->p.Y, q->r + LIB_STAMP_PAD);
    }
    if (a->ticket) lib_nav_cancel(&s->q, a->ticket);
    a->ticket = lib_nav_request(&s->q, a->pos, a->path.pts[a->path.n - 1], discs, nd);
    a->ticket_is_repath = true;
}

/* Av.walkTo: true when p is reached */
static bool walk_to(LibAgents *s, const LibNav *n, int self, HMM_Vec2 p, float dt, bool last)
{
    LibAgent *a = &s->a[self];
    int r = steer_step(s, n, self, p, dt, last); /* turns the facing too */
    if (r == 1) {
        a->halted = false;
        return true;
    }
    a->halted = r < 0;
    if (a->blocked_s > LIB_GHOST_AFTER_S) {
        a->ignore_others_s = LIB_GHOST_S;
        a->blocked_s = 0;
    } else if (a->blocked_s > LIB_REPATH_S && !a->repathed && a->has_path) {
        a->repathed = true;
        request_repath(s, self);
    }
    if (a->blocked_s < 0.2f) a->repathed = false;
    return false;
}

void lib_agents_step(LibAgents *s, const LibNav *n, float dt)
{
    if (!s->q.nav) s->q.nav = n;
    if (s->service_searches > 0) lib_nav_service_n(&s->q, s->service_searches);
    else lib_nav_service(&s->q, s->service_budget_ms);
    /* AG: everyone standing or walking, carts owned by their pusher */
    int si[LIB_AGENTS_MAX];
    s->nsnap = 0;
    for (int i = 0; i < s->count; i++) {
        const LibAgent *a = &s->a[i];
        si[i] = -1;
        if (!a->active) continue;
        int owner = a->is_cart && a->cart_of >= 0 ? a->cart_of : i;
        si[i] = s->nsnap;
        s->snap[s->nsnap++] = (LibAgentSnap){a->pos, a->radius, owner, s->a[owner].ignore_others_s > 0};
    }
    for (int i = 0; i < s->count; i++) {
        LibAgent *a = &s->a[i];
        if (!a->active || a->is_cart) continue;
        const int rate = a->rate > 1 ? a->rate : 1;
        if (rate > 1 && (s->tick + (uint32_t)i) % (uint32_t)rate) continue;
        const float adt = dt * (float)rate;
        if (a->ticket) {
            LibPath p;
            LibReqState st = lib_nav_poll(&s->q, a->ticket, &p);
            if (st == LIB_REQ_READY || st == LIB_REQ_FAILED || st == LIB_REQ_NONE) {
                a->ticket = 0;
                if (st != LIB_REQ_NONE) {
                    a->path = p;
                    a->next = 0;
                    a->has_path = a->path.n > 0;
                    if (!a->ticket_is_repath) a->arrived = !a->has_path;
                }
            }
        }
        HMM_Vec2 o = a->pos;
        a->on_last = false;
        a->cart_checked = false;
        if (a->has_path && !a->arrived && a->next < a->path.n) {
            a->standing_still = false;
            a->on_last = a->next == a->path.n - 1;
            if (walk_to(s, n, i, a->path.pts[a->next], adt, a->on_last)) {
                a->next++;
                if (a->next >= a->path.n) {
                    a->arrived = true;
                    a->has_path = false;
                    a->standing_still = true;
                }
            }
        } else if (!a->ticket) {
            a->standing_still = true;
        }
        a->vel = adt > 0 ? HMM_V2((a->pos.X - o.X) / adt, (a->pos.Y - o.Y) / adt) : HMM_V2(0, 0);
        if (a->cart >= 0 && a->cart_pushed) {
            LibAgent *c = &s->a[a->cart];
            HMM_Vec2 cp = HMM_V2(a->pos.X + sinf(a->yaw) * LIB_CART_AHEAD, a->pos.Y + cosf(a->yaw) * LIB_CART_AHEAD);
            const float far = dist2(cp, c->pos), most = LIB_CART_SLIDE * adt;
            float cy = a->yaw;
            if (a->cart_catching && far <= most + 1e-4f) a->cart_catching = false;
            if (a->cart_catching) { /* taken up again after a parked spell: it slides there, round anyone in the way */
                cp = HMM_V2(c->pos.X + (cp.X - c->pos.X) * most / far, c->pos.Y + (cp.Y - c->pos.Y) * most / far);
                cy = angle_lerp(c->yaw, a->yaw, ease(adt, 8));
            }
            /* the pusher checked this place (cart_ok) if it stepped this tick; otherwise (a cart still catching up) it is checked here */
            if (far > 1e-6f && (a->cart_catching || !a->cart_checked) && !snap_clear(s, i, cp, c->radius, c->pos)) cp = c->pos, cy = c->yaw;
            c->vel = adt > 0 ? HMM_V2((cp.X - c->pos.X) / adt, (cp.Y - c->pos.Y) / adt) : HMM_V2(0, 0);
            c->pos = cp;
            c->yaw = cy;
            if (si[a->cart] >= 0) s->snap[si[a->cart]].p = cp;
        }
        /* the prototype checks against positions from the start of the frame; keeping the snapshot current makes
         * the hard rule exact when two agents step towards each other in the same tick */
        if (si[i] >= 0) s->snap[si[i]].p = a->pos;
    }
    s->tick++;
}
