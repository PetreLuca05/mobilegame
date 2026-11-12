#include "nav_check.h"

#include <SDL3/SDL.h>
#include <cJSON.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/log.h"

#define RX0 (-34.0f)
#define RZ0 (-27.0f)
#define RNX 272
#define RNZ 256
#define RN (RNX * RNZ)
#define CS 0.25f
#define REF_STOREYS 4

static int b64v(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* base64 bitset (row-major j * nx + i, low bit first) -> one byte per cell */
static bool decode_bits(const char *s, uint8_t *out, int cells)
{
    memset(out, 0, (size_t)cells);
    uint32_t acc = 0;
    int bits = 0, byte = 0;
    for (const char *p = s; *p; p++) {
        int v = b64v((unsigned char)*p);
        if (v < 0) {
            continue;
        }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            uint8_t b = (uint8_t)(acc >> bits);
            for (int k = 0; k < 8; k++) {
                int c = byte * 8 + k;
                if (c < cells) {
                    out[c] = (uint8_t)((b >> k) & 1u);
                }
            }
            byte++;
        }
    }
    return byte * 8 >= cells;
}

static int ridx(float x, float z)
{
    int i = (int)floorf((x - RX0) / CS), j = (int)floorf((z - RZ0) / CS);
    return i < 0 || j < 0 || i >= RNX || j >= RNZ ? -1 : j * RNX + i;
}

static void rcentre(int k, float *x, float *z)
{
    *x = RX0 + ((float)(k % RNX) + 0.5f) * CS;
    *z = RZ0 + ((float)(k / RNX) + 0.5f) * CS;
}

typedef struct Rect {
    float x0, z0, x1, z1;
} Rect;

static Rect rect_of(float x, float z, float yaw, float w, float d)
{
    int q = ((int)lroundf(yaw / 90.0f) % 2 + 2) % 2;
    float hw = q ? d / 2 : w / 2, hd = q ? w / 2 : d / 2;
    return (Rect){x - hw, z - hd, x + hw, z + hd};
}

/* the page's door-approach rule: these kinds hang on walls or are flat, they never block a door */
static bool skip_kind(int k)
{
    return k == IMF_DOOR_LEAF || k == IMF_RUG || k == IMF_WALL_POSTER || k == IMF_CLOCK || k == IMF_NOTICE_POSTER ||
           k == IMF_WALL_SCREEN || k == IMF_LIFT_SIGN || k == IMF_PHARMACY_SIGN || k == IMF_TROPHY || k == IMF_COATS ||
           k == IMF_INFO_SCREEN;
}

static char *load_text(const char *path)
{
    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    return data;
}

static int cells_across(const uint8_t *w, float x, float z, float cx, float cz, float nx, float nz, float len)
{
    int best = 1 << 30;
    for (int oi = -1; oi <= 1; oi++) {
        const float o = 0.25f * (float)oi;
        int seen[64], ns = 0;
        for (float t = 0.0f; t <= len + 1e-6f; t += 0.05f) {
            int k = ridx(x + cx * t + nx * o, z + cz * t + nz * o);
            if (k < 0 || !w[k]) {
                continue;
            }
            bool dup = false;
            for (int i = 0; i < ns; i++) {
                dup = dup || seen[i] == k;
            }
            if (!dup && ns < 64) {
                seen[ns++] = k;
            }
        }
        best = ns < best ? ns : best;
    }
    return best;
}

/* the page's rule: a pocket behind a counter (within 0.8 m of one) is allowed */
static bool near_counter(const IMap *m, int storey, float x, float z)
{
    for (int i = 0; i < m->prop_count; i++) {
        const ImProp *q = &m->props[i];
        if (q->storey != storey || (q->kind != IMF_DISPENSING_COUNTER && q->kind != IMF_STATION_COUNTER &&
                                    q->kind != IMF_NURSE_COUNTER && q->kind != IMF_RECEPTION_DESK)) {
            continue;
        }
        Rect r = rect_of(q->x, q->z, q->yaw, q->w, q->d);
        if (x > r.x0 - .8f && x < r.x1 + .8f && z > r.z0 - .8f && z < r.z1 + .8f) {
            return true;
        }
    }
    return false;
}

/* the colliders that block a cell centre (capsule r 0.35 m, 0.35 .. 1.8 m above the floor) */
static void blockers_of(FILE *fp, const ColWorld *w, float x, float z, float fy)
{
    for (int i = 0; i < w->count; i++) {
        const ColBox *b = &w->boxes[i];
        if (b->aabb_max.Y <= fy + 0.35f || b->aabb_min.Y >= fy + 1.8f) {
            continue;
        }
        const float dx = x < b->aabb_min.X ? b->aabb_min.X - x : (x > b->aabb_max.X ? x - b->aabb_max.X : 0.0f);
        const float dz = z < b->aabb_min.Z ? b->aabb_min.Z - z : (z > b->aabb_max.Z ? z - b->aabb_max.Z : 0.0f);
        if (dx * dx + dz * dz < 0.35f * 0.35f) {
            fprintf(fp, "      cell %.3f,%.3f: collider %d x %.2f..%.2f z %.2f..%.2f y %.2f..%.2f\n", (double)x, (double)z, i,
                    (double)b->aabb_min.X, (double)b->aabb_max.X, (double)b->aabb_min.Z, (double)b->aabb_max.Z,
                    (double)b->aabb_min.Y, (double)b->aabb_max.Y);
        }
    }
}

int nav_check_write(const Institute *in, const NavWorld *n, const ColWorld *world, const char *path)
{
    const IMap *m = &in->map;
    const char *ref_path = SDL_getenv("MG_NAV_REF");
    char *json = load_text(ref_path && *ref_path ? ref_path : "assets/world/institute/model_nav.json");
    if (!json) {
        mg_log_error("nav_check: cannot read assets/world/institute/model_nav.json (run from the repository root or set MG_NAV_REF)");
        return 1;
    }
    cJSON *root = cJSON_Parse(json);
    SDL_free(json);
    cJSON *walk = root ? cJSON_GetObjectItem(cJSON_GetObjectItem(root, "grid"), "walk") : NULL;
    if (!walk || cJSON_GetArraySize(walk) < REF_STOREYS) {
        mg_log_error("nav_check: model_nav.json has no grid.walk");
        cJSON_Delete(root);
        return 1;
    }
    uint8_t *ref = SDL_malloc((size_t)RN * REF_STOREYS), *mine = SDL_malloc((size_t)RN * REF_STOREYS);
    uint8_t *reach = SDL_calloc((size_t)RN, REF_STOREYS), *seen = SDL_calloc((size_t)RN, 1);
    int32_t *stack = SDL_malloc(sizeof(int32_t) * (size_t)RN * REF_STOREYS * 2);
    FILE *fp = fopen(path, "w");
    if (!ref || !mine || !reach || !seen || !stack || !fp) {
        mg_log_error("nav_check: out of memory or cannot write %s", path);
        SDL_free(ref), SDL_free(mine), SDL_free(reach), SDL_free(seen), SDL_free(stack);
        if (fp) {
            fclose(fp);
        }
        cJSON_Delete(root);
        return 1;
    }
    for (int f = 0; f < REF_STOREYS; f++) {
        decode_bits(cJSON_GetArrayItem(walk, f)->valuestring, ref + (size_t)f * RN, RN);
        for (int k = 0; k < RN; k++) {
            float x, z;
            rcentre(k, &x, &z);
            mine[(size_t)f * RN + k] = (nav_flags_at(n, f, x, z) & NAV_WALKABLE) ? 1 : 0;
        }
    }
    int fails = 0;
    fprintf(fp, "MAP-2 nav check: baked grids (capsule r 0.35 m, h 1.8 m, step 0.35 m) vs assets/world/institute/model_nav.json\n");
    fprintf(fp, "lattice 0.25 m, x0 -34, z0 -27, 272 x 256 cells per storey (the reference has storeys 0..3)\n\n");

    /* 1. cell counts per storey */
    fprintf(fp, "== cells per storey (inside the reference extent; building = x -26..26, z -18..18)\n");
    fprintf(fp, "storey | ref walk | baked walk | both | ref only | baked only | agree %% | building: ref only | baked only\n");
    for (int f = 0; f < REF_STOREYS; f++) {
        int rw = 0, mw = 0, both = 0, ro = 0, mo = 0, bro = 0, bmo = 0;
        for (int k = 0; k < RN; k++) {
            const int a = ref[(size_t)f * RN + k], b = mine[(size_t)f * RN + k];
            float x, z;
            rcentre(k, &x, &z);
            const bool bld = x > -26 && x < 26 && z > -18 && z < 18;
            rw += a;
            mw += b;
            both += a && b;
            ro += a && !b;
            mo += !a && b;
            bro += bld && a && !b;
            bmo += bld && !a && b;
        }
        fprintf(fp, "%6d | %8d | %10d | %4d | %8d | %10d | %6.1f | %18d | %10d\n", f, rw, mw, both, ro, mo,
                100.0 * (double)both / (double)(rw + mo > 0 ? rw + mo : 1), bro, bmo);
    }

    /* 2. door openings and approaches */
    fprintf(fp, "\n== doorways (the map's door_leaf records, no leaf built; stairwells exempt): cells across the opening (min of 3 rows), approach 1.0 m both sides\n");
    int doors = 0, narrow = 0, blocked_data = 0, blocked_grid = 0, ref_narrow = 0, approach_1m = 0;
    for (int i = 0; i < m->prop_count; i++) {
        const ImProp *p = &m->props[i];
        if (p->kind != IMF_DOOR_LEAF) {
            continue;
        }
        const ImRoom *r = p->room >= 0 ? &m->rooms[p->room] : NULL;
        const bool stairs = r && (r->flags & IMAP_RF_STAIRS);
        const float a = p->yaw * HMM_PI32 / 180.0f, cx = roundf(sinf(a)), cz = roundf(cosf(a));
        const float nx = p->open_x, nz = p->open_z, len = p->w;
        const uint8_t *w = mine + (size_t)p->storey * RN, *rw = ref + (size_t)p->storey * RN;
        const int across = cells_across(w, p->x, p->z, cx, cz, nx, nz, len);
        const int racross = cells_across(rw, p->x, p->z, cx, cz, nx, nz, len);
        /* the page's zone: the opening and 1.0 m on both sides; furniture and free book props overlapping it */
        const float ex = p->x + cx * len, ez = p->z + cz * len;
        float zx0 = fminf(p->x, ex), zx1 = fmaxf(p->x, ex), zz0 = fminf(p->z, ez), zz1 = fmaxf(p->z, ez);
        Rect zone = {fminf(zx0, fminf(zx0 + nx, zx0 - nx)), fminf(zz0, fminf(zz0 + nz, zz0 - nz)),
                     fmaxf(zx1, fmaxf(zx1 + nx, zx1 - nx)), fmaxf(zz1, fmaxf(zz1 + nz, zz1 - nz))};
        char blockers[256] = "";
        int nb = 0;
        for (int k = 0; k < m->prop_count; k++) {
            const ImProp *q = &m->props[k];
            if (q->storey != p->storey || skip_kind(q->kind) || q->mount == IM_MOUNT_FLOOR) {
                continue;
            }
            Rect rr = rect_of(q->x, q->z, q->yaw, q->w, q->d);
            if (rr.x0 < zone.x1 - .02f && zone.x0 < rr.x1 - .02f && rr.z0 < zone.z1 - .02f && zone.z0 < rr.z1 - .02f) {
                nb++;
                if (strlen(blockers) < 200) {
                    strncat(blockers, imap_str(m, q->id), 255 - strlen(blockers));
                    strncat(blockers, " ", 255 - strlen(blockers));
                }
            }
        }
        for (int k = 0; k < m->iprop_count; k++) {
            const ImIProp *q = &m->iprops[k];
            float iw, id;
            bool wall;
            furn_iprop_size(q->kind, &iw, &id, &wall);
            if (q->storey != p->storey || q->furniture >= 0 || wall) {
                continue;
            }
            Rect rr = rect_of(q->x, q->z, q->yaw, iw, id);
            if (rr.x0 < zone.x1 - .02f && zone.x0 < rr.x1 - .02f && rr.z0 < zone.z1 - .02f && zone.z0 < rr.z1 - .02f) {
                nb++;
                if (strlen(blockers) < 200) {
                    strncat(blockers, imap_str(m, q->id), 255 - strlen(blockers));
                    strncat(blockers, " ", 255 - strlen(blockers));
                }
            }
        }
        /* the baked grid: the opening's centre line, 0.25 .. 1.0 m out on both sides */
        const float mx = p->x + cx * len * 0.5f, mz = p->z + cz * len * 0.5f;
        int miss = 0, miss1 = 0;
        for (int s = -1; s <= 1; s += 2) {
            for (int dstep = 1; dstep <= 4; dstep++) {
                const float dd = 0.25f * (float)dstep * (float)s;
                const int k = ridx(mx + nx * dd, mz + nz * dd);
                if (dstep < 4) {
                    miss += k < 0 || !w[k];
                } else {
                    miss1 += k < 0 || !w[k];
                }
            }
        }
        approach_1m += miss1 > 0;
        if (stairs) {
            continue;
        }
        doors++;
        narrow += across < 2;
        ref_narrow += racross < 2;
        blocked_data += nb > 0;
        blocked_grid += miss > 0;
        if (across < 2 || nb > 0 || miss > 0 || miss1 > 0 || across != racross) {
            fprintf(fp, "  %-28s storey %d at %6.2f,%6.2f len %.2f: across %d (ref %d)%s%s%s%s\n", imap_str(m, p->id), p->storey,
                    (double)p->x, (double)p->z, (double)len, across, racross, across < 2 ? " NARROW" : "",
                    miss ? " approach-cells-blocked" : (miss1 ? " (the cell 1.0 m out is within 0.35 m of furniture)" : ""), nb ? " furniture-in-zone: " : "", nb ? blockers : "");
            for (int s = -1; s <= 1 && (miss || miss1); s += 2) {
                for (int dstep = 1; dstep <= 4; dstep++) {
                    const float dd = 0.25f * (float)dstep * (float)s;
                    const int k = ridx(mx + nx * dd, mz + nz * dd);
                    if (k >= 0 && !w[k]) {
                        float cxw, czw;
                        rcentre(k, &cxw, &czw);
                        blockers_of(fp, world, cxw, czw, (float)p->storey * INST_FH);
                    }
                }
            }
        }
    }
    fprintf(fp, "doorways %d: %d with < 2 cells across (reference: %d), %d with furniture in the 1.0 m zone (the page's "
                "check), %d with a blocked baked cell 0.25 .. 0.75 m out, %d with the cell 1.0 m out next to furniture\n",
            doors, narrow, ref_narrow, blocked_data, blocked_grid, approach_1m);
    fails += narrow + blocked_data + blocked_grid;

    /* 3. reachability from the entrance, lifts joining the storeys */
    int start = -1;
    for (int i = 0; i < m->room_count && start < 0; i++) {
        const ImRoom *r = &m->rooms[i];
        if (r->flags & IMAP_RF_ENTRANCE) {
            const float sx = (r->x0 + r->x1) * 0.5f, sz = (r->z0 + r->z1) * 0.5f - 1.5f;
            for (int rad = 0; rad < 20 && start < 0; rad++) {
                for (int dj = -rad; dj <= rad && start < 0; dj++) {
                    for (int di = -rad; di <= rad && start < 0; di++) {
                        int k = ridx(sx + (float)di * CS, sz + (float)dj * CS);
                        if (k >= 0 && mine[k]) {
                            start = k;
                        }
                    }
                }
            }
        }
    }
    int sp = 0;
    bool cab_done[INST_LIFTS] = {false};
    if (start >= 0) {
        reach[start] = 1;
        stack[sp++] = start;
    }
    while (sp > 0) {
        const int e = stack[--sp], f = e / RN, k = e % RN;
        float x, z;
        rcentre(k, &x, &z);
        for (int l = 0; l < INST_LIFTS; l++) {
            if (!cab_done[l] && fabsf(x - in->lift_x[l]) < 1.05f && z > 4.95f && z < 7.75f) {
                cab_done[l] = true;
                for (int g2 = 0; g2 < REF_STOREYS; g2++) {
                    const int kk = ridx(in->lift_x[l], 6.35f);
                    if (kk >= 0 && mine[(size_t)g2 * RN + kk] && !reach[(size_t)g2 * RN + kk]) {
                        reach[(size_t)g2 * RN + kk] = 1;
                        stack[sp++] = g2 * RN + kk;
                    }
                }
            }
        }
        const int ki = k % RNX, kj = k / RNX;
        static const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int q = 0; q < 4; q++) {
            const int ni = ki + d4[q][0], nj = kj + d4[q][1];
            if (ni < 0 || nj < 0 || ni >= RNX || nj >= RNZ) {
                continue;
            }
            const size_t nn = (size_t)f * RN + (size_t)(nj * RNX + ni);
            if (mine[nn] && !reach[nn]) {
                reach[nn] = 1;
                stack[sp++] = (int)nn;
            }
        }
    }
    fprintf(fp, "\n== rooms (cells with their centre inside the rectangle): baked walkable / reachable from the entrance vs the "
                "reference; pockets = walkable but unreachable areas >= 0.5 m2\n");
    int rooms_bad = 0, rooms_diff = 0;
    for (int i = 0; i < m->room_count; i++) {
        const ImRoom *r = &m->rooms[i];
        const bool stairs = (r->flags & IMAP_RF_STAIRS) != 0;
        const uint8_t *w = mine + (size_t)r->storey * RN, *rw = ref + (size_t)r->storey * RN, *rr = reach + (size_t)r->storey * RN;
        int cells = 0, reached = 0, rcells = 0, pockets = 0;
        float pocket_m2 = 0.0f;
        for (float z = r->z0 + CS / 2; z < r->z1; z += CS) {
            for (float x = r->x0 + CS / 2; x < r->x1; x += CS) {
                const int k = ridx(x, z);
                if (k < 0) {
                    continue;
                }
                rcells += rw[k];
                if (!w[k]) {
                    continue;
                }
                cells++;
                reached += rr[k];
                if (!rr[k] && !seen[k]) { /* a pocket: flood it inside the room */
                    int ps = 0, cnt = 0;
                    bool counter = false;
                    stack[ps++] = k;
                    seen[k] = 1;
                    while (ps > 0) {
                        const int c = stack[--ps];
                        cnt++;
                        float qx, qz;
                        rcentre(c, &qx, &qz);
                        counter = counter || near_counter(m, r->storey, qx, qz);
                        const int ci = c % RNX, cj = c / RNX;
                        static const int d4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                        for (int q = 0; q < 4; q++) {
                            const int ni = ci + d4[q][0], nj = cj + d4[q][1];
                            if (ni < 0 || nj < 0 || ni >= RNX || nj >= RNZ) {
                                continue;
                            }
                            const int nk = nj * RNX + ni;
                            float px, pz;
                            rcentre(nk, &px, &pz);
                            if (px > r->x0 && px < r->x1 && pz > r->z0 && pz < r->z1 && w[nk] && !rr[nk] && !seen[nk]) {
                                seen[nk] = 1;
                                stack[ps++] = nk;
                            }
                        }
                    }
                    const float m2 = (float)cnt * CS * CS;
                    if (m2 >= 0.5f && counter) {
                        fprintf(fp, "  %-14s storey %d: %.2f m2 pocket behind a counter (allowed)\n", imap_str(m, r->id), r->storey,
                                (double)m2);
                    } else if (m2 >= 0.5f) {
                        float ax, az;
                        rcentre(k, &ax, &az);
                        fprintf(fp, "  %-14s storey %d: %.2f m2 unreachable pocket at %.2f,%.2f\n", imap_str(m, r->id), r->storey,
                                (double)m2, (double)ax, (double)az);
                        blockers_of(fp, world, ax, az - 0.25f, (float)r->storey * INST_FH);
                        blockers_of(fp, world, ax, az + 0.25f, (float)r->storey * INST_FH);
                        blockers_of(fp, world, ax - 0.25f, az, (float)r->storey * INST_FH);
                        blockers_of(fp, world, ax + 0.25f, az, (float)r->storey * INST_FH);
                        pockets++;
                        pocket_m2 += m2;
                    }
                }
            }
        }
        const bool bad = !stairs && (reached == 0 || pockets > 0);
        const bool diff = abs(cells - rcells) > (rcells / 10 > 8 ? rcells / 10 : 8);
        rooms_bad += bad;
        rooms_diff += diff;
        if (bad || diff) {
            fprintf(fp, "  %-14s storey %d %-13s baked %4d (reachable %4d) ref %4d  diff %+5d%s%s\n", imap_str(m, r->id),
                    r->storey, stairs ? "(stairwell)" : "", cells, reached, rcells, cells - rcells,
                    bad ? (reached == 0 ? "  NOT REACHABLE" : "  POCKETS") : "", pockets ? "" : "");
            if (pockets) {
                fprintf(fp, "      %d unreachable pocket(s), %.2f m2\n", pockets, (double)pocket_m2);
            }
        }
    }
    fprintf(fp, "rooms %d: %d fail reachability (stairwells exempt), %d differ from the reference by > 10 %% of their cells\n",
            m->room_count, rooms_bad, rooms_diff);
    fails += rooms_bad;
    fprintf(fp, "\nRESULT: %s (%d failed checks)\n", fails ? "FAIL" : "PASS", fails);
    fclose(fp);
    mg_log_info("nav_check: %d doorways (%d narrow, %d furniture in the approach, %d blocked approach cells), %d rooms "
                "unreachable or with pockets, %d rooms differ from the reference: %s -> %s",
                doors, narrow, blocked_data, blocked_grid, rooms_bad, rooms_diff, fails ? "FAIL" : "PASS", path);
    SDL_free(ref);
    SDL_free(mine);
    SDL_free(reach);
    SDL_free(seen);
    SDL_free(stack);
    cJSON_Delete(root);
    return fails;
}
