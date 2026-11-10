/* LIB-1 lobby dressing (see lib_dressing.h). Doubles throughout, as in the JS it reproduces. */
#include "lib_dressing.h"

#include <math.h>
#include <string.h>

static void fnv_unit(uint32_t *h, uint32_t unit)
{
    *h ^= unit;
    *h *= 16777619u;
}

/* One UTF-8 sequence -> code point; a malformed byte stands for itself. */
static uint32_t utf8_next(const unsigned char **p)
{
    const unsigned char *s = *p;
    uint32_t c = s[0];
    int n = c >= 0xF0 && c < 0xF8 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (c >= 0x80 && c < 0xC0) n = 0;
    for (int i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80) { *p = s + 1; return c; }
    }
    if (n) c &= 0x3Fu >> n;
    for (int i = 1; i <= n; i++) c = (c << 6) | (s[i] & 0x3Fu);
    *p = s + 1 + n;
    return c;
}

uint32_t lib_seed_of(const char *user_id)
{
    uint32_t h = 2166136261u;
    const unsigned char *p = (const unsigned char *)(user_id ? user_id : "");
    while (*p) {
        uint32_t c = utf8_next(&p);
        if (c > 0xFFFF) {
            c -= 0x10000;
            fnv_unit(&h, 0xD800 + (c >> 10));
            fnv_unit(&h, 0xDC00 + (c & 0x3FF));
        } else {
            fnv_unit(&h, c);
        }
    }
    return h;
}

double lib_rng_nextd(LibRng *r)
{
    uint32_t s = r->s += 0x6D2B79F5u;
    uint32_t t = (s ^ (s >> 15)) * (1u | s);
    t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
    return (double)(t ^ (t >> 14)) / 4294967296.0;
}

float lib_rng_next(LibRng *r) { return (float)lib_rng_nextd(r); }

static int rng_index(LibRng *r, int n) { return (int)(lib_rng_nextd(r) * n); }

static void shuffle_ints(int *a, int n, LibRng *r)
{
    for (int i = n - 1; i > 0; i--) {
        int j = rng_index(r, i + 1), t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

static bool read_pair(const cJSON *arr, double out[2])
{
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) < 2) return false;
    const cJSON *x = cJSON_GetArrayItem(arr, 0), *z = cJSON_GetArrayItem(arr, 1);
    if (!cJSON_IsNumber(x) || !cJSON_IsNumber(z)) return false;
    out[0] = x->valuedouble;
    out[1] = z->valuedouble;
    return true;
}

static int read_pairs(const cJSON *arr, double (*out)[2], int max)
{
    int n = 0;
    const cJSON *e;
    if (!cJSON_IsArray(arr)) return 0;
    cJSON_ArrayForEach(e, arr) {
        if (n >= max || !read_pair(e, out[n])) return 0;
        n++;
    }
    return n;
}

static bool read_number(const cJSON *o, const char *key, double *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v)) return false;
    *out = v->valuedouble;
    return true;
}

bool lib_dressing_config(LibDressConfig *c, const cJSON *scene_root)
{
    memset(c, 0, sizeof *c);
    const cJSON *d = cJSON_GetObjectItemCaseSensitive(scene_root, "lobby_dressing");
    const cJSON *hall = cJSON_GetObjectItemCaseSensitive(scene_root, "hall");
    if (!cJSON_IsObject(d) || !cJSON_IsObject(hall)) return false;
    c->lamp_count = read_pairs(cJSON_GetObjectItemCaseSensitive(d, "lamp_anchors"), c->lamp_anchors, LIB_DRESS_MAX_LAMPS);
    c->anchor_count = read_pairs(cJSON_GetObjectItemCaseSensitive(d, "anchors"), c->anchors, LIB_DRESS_MAX_ANCHORS);
    const cJSON *pool = cJSON_GetObjectItemCaseSensitive(d, "pool"), *e;
    if (!cJSON_IsArray(pool)) return false;
    cJSON_ArrayForEach(e, pool) {
        if (!cJSON_IsString(e) || c->pool_count >= LIB_DRESS_MAX_POOL || strlen(e->valuestring) >= LIB_DRESS_NAME) return false;
        strcpy(c->pool[c->pool_count++], e->valuestring);
    }
    double count[2], bays;
    if (!read_pair(cJSON_GetObjectItemCaseSensitive(d, "count"), count) || !read_number(d, "clear", &c->clear) ||
        !read_number(d, "jitter", &c->jitter) || !read_number(hall, "bays", &bays))
        return false;
    c->count_min = (int)count[0];
    c->count_max = (int)count[1];
    c->bays = (int)bays;
    return c->lamp_count > 0 && c->anchor_count > 0 && c->pool_count > 0 && c->count_min >= 0 &&
           c->count_max >= c->count_min && c->bays > 0;
}

static void dress_clutter(const LibDressConfig *c, LibRng *r, const double lamp[2], LibDressing *out)
{
    int free_ix[LIB_DRESS_MAX_ANCHORS], nfree = 0, pool[LIB_DRESS_MAX_POOL];
    for (int i = 0; i < c->anchor_count; i++)
        if (hypot(c->anchors[i][0] - lamp[0], c->anchors[i][1] - lamp[1]) > c->clear) free_ix[nfree++] = i;
    shuffle_ints(free_ix, nfree, r);
    for (int i = 0; i < c->pool_count; i++) pool[i] = i;
    shuffle_ints(pool, c->pool_count, r);
    int n = c->count_min + rng_index(r, c->count_max - c->count_min + 1);
    if (n > nfree) n = nfree;
    if (n > c->pool_count) n = c->pool_count;
    if (n > LIB_DRESS_MAX_ITEMS) n = LIB_DRESS_MAX_ITEMS;
    for (int k = 0; k < n; k++) {
        const double *a = c->anchors[free_ix[k]];
        LibDressItem *it = &out->items[k];
        double x = a[0] + (lib_rng_nextd(r) * 2.0 - 1.0) * c->jitter;
        double z = a[1] + (lib_rng_nextd(r) * 2.0 - 1.0) * c->jitter;
        double yaw = lib_rng_nextd(r) * 6.283185307179586;
        memcpy(it->name, c->pool[pool[k]], LIB_DRESS_NAME);
        it->x = (float)x;
        it->z = (float)z;
        it->yaw = (float)yaw;
        it->anchor = free_ix[k];
    }
    out->item_count = n;
}

void lib_dress(const LibDressConfig *c, uint32_t seed, LibDressing *out)
{
    LibRng r = lib_rng(seed);
    memset(out, 0, sizeof *out);
    out->seed = seed;
    out->bay = -1;
    for (int i = 0; i < 4; i++) out->seat_order[i] = i;
    shuffle_ints(out->seat_order, 4, &r);
    for (int n = 0; n < 4; n++) out->cover[out->seat_order[n]] = rng_index(&r, LIB_COVER_COUNT);
    out->lamp_anchor = rng_index(&r, c->lamp_count);
    const double *lamp = c->lamp_anchors[out->lamp_anchor];
    out->lamp_x = (float)lamp[0];
    out->lamp_z = (float)lamp[1];
    out->lamp_yaw = (float)((lib_rng_nextd(&r) - 0.5) * 0.5);
    dress_clutter(c, &r, lamp, out);
}

int lib_table_bay(uint32_t seed, int bays, int bay_i_am_in)
{
    if (bays <= 0) return 0;
    int b = (int)(seed % (uint32_t)bays);
    return b == bay_i_am_in ? (b + 1) % bays : b;
}

uint32_t lib_empty_table_seed(int bay) { return LIB_EMPTY_SEED_BASE + LIB_EMPTY_SEED_STEP * (uint32_t)bay; }
