/* LIB-1 lobby dressing unit test (own main). Usage: lib_dressing_test [path/to/library.scene.json]
 * (1) tables_now reproduces exactly, (2) 1,000 seeds keep clear of the lamp and of every seat's places, 3-5 items,
 * >= 200 distinct layouts, (3) lib_seed_of against FNV-1a values computed independently (Python, UTF-16 units).
 *
 * Seat places, in the seat frame (forward +z, the sitter's left +x), turned by seats[i].yaw_deg into the table frame:
 *   book      the open book: centre (0, book.forward), half size 0.25 x 0.17 (the prototype's openBook, 0.50 x 0.34)
 *   notebook  the notebook inside book_open: centre (-0.45, book.forward - 0.06), half size 0.10 x 0.135
 *   pencil    life.fidget.notes.pencil_rest, radius 0.08
 *   mug       life.mug.rest, radius 0.06
 *   phone     life.fidget.phone.table (lying on the table), radius 0.09
 * An item's centre must lie outside each footprint grown by ITEM_MARGIN, and farther than `clear` from the lamp. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib_dressing.h"

#define ITEM_MARGIN 0.05
#define RANDOM_SEEDS 1000
#define MIN_LAYOUTS 200

static int g_fail;

static void check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fail = 1;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    if (buf) buf[n] = 0;
    fclose(f);
    return buf;
}

static double num(const cJSON *o, const char *key) { return cJSON_GetObjectItemCaseSensitive(o, key)->valuedouble; }
static double at(const cJSON *arr, int i) { return cJSON_GetArrayItem(arr, i)->valuedouble; }
static const cJSON *obj(const cJSON *o, const char *key) { return cJSON_GetObjectItemCaseSensitive(o, key); }

/* ---- (1) tables_now ---- */

static bool same3(float v, double want) { return llround((double)v * 1000.0) == llround(want * 1000.0); }
static bool same2(float v, double want) { return llround((double)v * 100.0) == llround(want * 100.0); }

static bool table_matches(const LibDressConfig *c, const cJSON *t, const cJSON *root)
{
    uint32_t seed = (uint32_t)num(t, "seed");
    int bay = (int)num(t, "bay"), home = (int)num(obj(root, "hall"), "home");
    LibDressing d;
    lib_dress(c, seed, &d);
    /* empty tables use 9000 + 131 * bay; the home table holds the prototype's hero ("Luca") */
    bool ok = seed == lib_empty_table_seed(bay) || (bay == home && seed == lib_seed_of("Luca"));
    const cJSON *order = obj(t, "seat_order"), *lamp = obj(t, "lamp"), *items = obj(t, "items");
    for (int i = 0; i < 4; i++) ok = ok && d.seat_order[i] == (int)at(order, i);
    ok = ok && d.lamp_x == (float)at(lamp, 0) && d.lamp_z == (float)at(lamp, 1);
    ok = ok && d.item_count == cJSON_GetArraySize(items);
    for (int k = 0; ok && k < d.item_count; k++) {
        const cJSON *it = cJSON_GetArrayItem(items, k);
        const LibDressItem *m = &d.items[k];
        ok = !strcmp(m->name, obj(it, "kind")->valuestring) && same3(m->x, num(it, "x")) && same3(m->z, num(it, "z")) &&
             same2(m->yaw, num(it, "yaw"));
    }
    printf("     seed %u bay %d: order %d%d%d%d lamp %d (%.2f, %.2f) yaw %.3f, %d items:", seed, bay, d.seat_order[0],
           d.seat_order[1], d.seat_order[2], d.seat_order[3], d.lamp_anchor, d.lamp_x, d.lamp_z, d.lamp_yaw, d.item_count);
    for (int k = 0; k < d.item_count; k++)
        printf(" %s(%.3f, %.3f, %.2f)", d.items[k].name, d.items[k].x, d.items[k].z, d.items[k].yaw);
    printf("\n");
    return ok;
}

/* ---- (2) places ---- */

typedef struct Place { double cx, cz, hx, hz, r, yaw; const char *what; } Place; /* rect if r == 0 */

static int g_place_count;
static Place g_places[32];

static void add_place(const cJSON *seat, double lx, double lz, double hx, double hz, double r, const char *what)
{
    const cJSON *pos = obj(seat, "pos");
    double yaw = num(seat, "yaw_deg") * 3.141592653589793 / 180.0, cs = cos(yaw), sn = sin(yaw);
    Place *p = &g_places[g_place_count++];
    p->cx = at(pos, 0) + lx * cs + lz * sn;
    p->cz = at(pos, 1) - lx * sn + lz * cs;
    p->hx = hx;
    p->hz = hz;
    p->r = r;
    p->yaw = yaw;
    p->what = what;
}

static void build_places(const cJSON *root)
{
    const cJSON *seat, *life = obj(root, "life"), *fid = obj(life, "fidget");
    double fwd = num(obj(root, "book"), "forward");
    const cJSON *mug = obj(obj(life, "mug"), "rest"), *pencil = obj(obj(fid, "notes"), "pencil_rest");
    const cJSON *phone = obj(obj(fid, "phone"), "table");
    cJSON_ArrayForEach(seat, obj(root, "seats")) {
        add_place(seat, 0.0, fwd, 0.25, 0.17, 0.0, "book");
        add_place(seat, -0.45, fwd - 0.06, 0.10, 0.135, 0.0, "notebook");
        add_place(seat, at(pencil, 0), at(pencil, 2), 0, 0, 0.08, "pencil");
        add_place(seat, at(mug, 0), at(mug, 2), 0, 0, 0.06, "mug");
        add_place(seat, at(phone, 0), at(phone, 2), 0, 0, 0.09, "phone");
    }
}

/* Distance from (x, z) to the place's footprint (0 inside). */
static double place_dist(const Place *p, double x, double z)
{
    double dx = x - p->cx, dz = z - p->cz;
    if (p->r > 0.0) return fmax(0.0, hypot(dx, dz) - p->r);
    double cs = cos(p->yaw), sn = sin(p->yaw);
    double lx = fabs(dx * cs - dz * sn) - p->hx, lz = fabs(dx * sn + dz * cs) - p->hz;
    return hypot(fmax(lx, 0.0), fmax(lz, 0.0));
}

static uint32_t xorshift(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

static void layout_key(const LibDressing *d, char *out, size_t n)
{
    int w = snprintf(out, n, "%d", d->lamp_anchor);
    for (int k = 0; k < d->item_count && w > 0 && (size_t)w < n; k++)
        w += snprintf(out + w, n - (size_t)w, "|%s@%d", d->items[k].name, d->items[k].anchor);
}

static void random_seeds(const LibDressConfig *c)
{
    static char keys[RANDOM_SEEDS][256];
    int nkeys = 0, bad_lamp = 0, bad_place = 0, bad_count = 0;
    double worst = 1e9;
    const char *worst_what = "-";
    uint32_t s = 0x2545F491u;
    for (int i = 0; i < RANDOM_SEEDS; i++) {
        LibDressing d;
        lib_dress(c, xorshift(&s), &d);
        if (d.item_count < 3 || d.item_count > 5) bad_count++;
        for (int k = 0; k < d.item_count; k++) {
            const LibDressItem *it = &d.items[k];
            if (hypot(it->x - d.lamp_x, it->z - d.lamp_z) <= c->clear) bad_lamp++;
            for (int p = 0; p < g_place_count; p++) {
                double dist = place_dist(&g_places[p], it->x, it->z);
                if (dist < worst) { worst = dist; worst_what = g_places[p].what; }
                if (dist <= ITEM_MARGIN) bad_place++;
            }
        }
        char key[256];
        layout_key(&d, key, sizeof key);
        int j = 0;
        while (j < nkeys && strcmp(keys[j], key)) j++;
        if (j == nkeys) strcpy(keys[nkeys++], key);
    }
    char msg[160];
    snprintf(msg, sizeof msg, "1000 seeds: no item within clear (%.2f) of the lamp (%d bad)", c->clear, bad_lamp);
    check(bad_lamp == 0, msg);
    snprintf(msg, sizeof msg, "1000 seeds: no item on a seat's book/notebook/pencil/mug/phone (%d bad; closest %.3f m to a %s)",
             bad_place, worst, worst_what);
    check(bad_place == 0, msg);
    snprintf(msg, sizeof msg, "1000 seeds: 3-5 items each (%d bad)", bad_count);
    check(bad_count == 0, msg);
    snprintf(msg, sizeof msg, "1000 seeds: %d distinct layouts (>= %d)", nkeys, MIN_LAYOUTS);
    check(nkeys >= MIN_LAYOUTS, msg);
}

/* ---- (3) seeds of ids ---- */

static void seed_strings(void)
{
    static const struct { const char *s; uint32_t h; } k[] = {
        {"", 0x811c9dc5u},     {"a", 0xe40c292cu},        {"foobar", 0xbf9cf968u},
        {"Luca", 0x813c4f4eu}, {"\xC4\x83\xC8\x98", 0x2cfb769eu}, /* "ăȘ" */
        {"x\xF0\x9F\x98\x80", 0x71c60a4au},                      /* "x" + U+1F600, a surrogate pair */
        {"8f14e45f-ceea-467a-9a5e-1c1a2b3c4d5e", 0xacb470d2u},
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        char msg[96];
        uint32_t h = lib_seed_of(k[i].s);
        snprintf(msg, sizeof msg, "lib_seed_of #%zu = 0x%08x (want 0x%08x)", i, h, k[i].h);
        check(h == k[i].h, msg);
    }
}

static void bays(void)
{
    check(lib_table_bay(10, 4, -1) == 2 && lib_table_bay(10, 4, 2) == 3 && lib_table_bay(11, 4, 3) == 0 &&
              lib_empty_table_seed(3) == 9393u,
          "lib_table_bay / lib_empty_table_seed");
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "assets/menu/library.scene.json";
    char *text = read_file(path);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    LibDressConfig c;
    if (!root || !lib_dressing_config(&c, root)) {
        printf("FAIL cannot load %s\n", path);
        return 1;
    }
    const cJSON *t;
    int i = 0;
    cJSON_ArrayForEach(t, obj(root, "tables_now")) {
        char msg[64];
        snprintf(msg, sizeof msg, "tables_now[%d] reproduces exactly", i++);
        check(table_matches(&c, t, root), msg);
    }
    check(i == 4, "tables_now has four tables");
    build_places(root);
    random_seeds(&c);
    seed_strings();
    bays();
    cJSON_Delete(root);
    free(text);
    printf("%s\n", g_fail ? "LIB DRESSING TEST FAILED" : "ALL LIB DRESSING TESTS PASSED");
    return g_fail;
}
