#include "imap.h"

#include <string.h>

#include "core/assets.h"
#include "core/log.h"

/* record sizes written by tools/mapc.py (FMT) */
_Static_assert(sizeof(ImFloor) == 8, "ImFloor");
_Static_assert(sizeof(ImRoom) == 44, "ImRoom");
_Static_assert(sizeof(ImDoor) == 16, "ImDoor");
_Static_assert(sizeof(ImRect) == 20, "ImRect");
_Static_assert(sizeof(ImLift) == 16, "ImLift");
_Static_assert(sizeof(ImPoint) == 8, "ImPoint");
_Static_assert(sizeof(ImSlot) == 16, "ImSlot");
_Static_assert(sizeof(ImISlot) == 24, "ImISlot");
_Static_assert(sizeof(ImIProp) == 28, "ImIProp");
_Static_assert(sizeof(ImProp) == 44, "ImProp");
_Static_assert(sizeof(ImSite) == 28, "ImSite");
_Static_assert(sizeof(ImSpawn) == 12, "ImSpawn");
_Static_assert(sizeof(ImLearn) == 16, "ImLearn");

enum {
    T_STRINGS, T_FLOORS, T_ROOMS, T_DOORS, T_WINDOWS, T_CORRIDORS, T_VOIDS, T_LIFTS, T_STAIRS, T_SLOTS, T_ISLOTS,
    T_IPROPS, T_PROPS, T_SITE, T_SPAWNS, T_LEARN, T_ROOM_TYPES, T_FURN_KINDS, T_IPROP_KINDS, T_COUNT
};
static const uint32_t k_rec_size[T_COUNT] = {1, 8, 44, 16, 16, 20, 20, 16, 8, 16, 24, 28, 44, 28, 12, 16, 4, 4, 4};

static const char *const k_room_types[IM_ROOM_TYPE_COUNT] = {
    "exam", "lab", "ward_room", "icu_bay", "or", "scrub", "sterile", "recovery", "pharmacy", "meeting",
    "office", "reception", "treatment_bay", "nurse_station", "toilet", "store", "terrace", "imaging", "waiting", "triage"};
#define X(e, s) s,
static const char *const k_furn_kinds[IM_FURN_KIND_COUNT] = {IM_FURN_KINDS(X)};
#undef X
static const char *const k_iprop_kinds[IMP_KIND_COUNT] = {"poster", "plaque", "board", "lightbox", "shelf",
                                                          "cabinet", "counter", "bench", "cart", "monitor",
                                                          "scope_tower", "model", "podium", "microscope", "iv_stand"};

const char *imap_furn_kind_name(int kind) { return kind >= 0 && kind < IM_FURN_KIND_COUNT ? k_furn_kinds[kind] : "?"; }

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool str_ok(const IMap *m, uint32_t off) { return off < m->strings_size; }

static bool names_match(const IMap *m, const unsigned char *tab, uint32_t count, const char *const *want, uint32_t n,
                        const char *what)
{
    if (count != n) {
        mg_log_error("imap: %u %s names, the engine knows %u", count, what, n);
        return false;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t off = rd32(tab + i * 4);
        if (!str_ok(m, off) || strcmp(m->strings + off, want[i]) != 0) {
            mg_log_error("imap: %s %u is \"%s\", the engine expects \"%s\" (rebuild: tools/mapc.py and imap.h differ)", what,
                         i, str_ok(m, off) ? m->strings + off : "?", want[i]);
            return false;
        }
    }
    return true;
}

#define CHECK(cond, ...)                                                                                                 \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            mg_log_error(__VA_ARGS__);                                                                                 \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

bool imap_open(IMap *m, const void *blob, size_t size)
{
    memset(m, 0, sizeof(*m));
    const unsigned char *b = blob;
    CHECK(b && size >= 16 && ((uintptr_t)b & 7u) == 0, "imap: blob missing or not 8-byte aligned");
    CHECK(memcmp(b, "MGMP", 4) == 0 && rd32(b + 4) == 1, "imap: bad magic or version");
    const uint32_t ntab = rd32(b + 8);
    CHECK(ntab == T_COUNT && 16 + (size_t)ntab * 12 <= size, "imap: %u tables, expected %d", ntab, T_COUNT);
    const unsigned char *tab[T_COUNT];
    uint32_t count[T_COUNT];
    for (int t = 0; t < T_COUNT; t++) {
        const uint32_t off = rd32(b + 16 + t * 12), n = rd32(b + 20 + t * 12), rs = rd32(b + 24 + t * 12);
        CHECK(rs == k_rec_size[t], "imap: table %d record size %u, expected %u", t, rs, k_rec_size[t]);
        CHECK((off & 7u) == 0 && (uint64_t)off + (uint64_t)n * rs <= size, "imap: table %d out of bounds", t);
        tab[t] = b + off;
        count[t] = n;
    }
    m->strings = (const char *)tab[T_STRINGS];
    m->strings_size = count[T_STRINGS];
    CHECK(m->strings_size > 0 && m->strings[0] == '\0' && m->strings[m->strings_size - 1] == '\0',
          "imap: string table not NUL-terminated");
    if (!names_match(m, tab[T_ROOM_TYPES], count[T_ROOM_TYPES], k_room_types, IM_ROOM_TYPE_COUNT, "room type") ||
        !names_match(m, tab[T_FURN_KINDS], count[T_FURN_KINDS], k_furn_kinds, IM_FURN_KIND_COUNT, "furniture kind") ||
        !names_match(m, tab[T_IPROP_KINDS], count[T_IPROP_KINDS], k_iprop_kinds, IMP_KIND_COUNT, "book prop kind")) {
        return false;
    }
    m->floors = (const ImFloor *)(const void *)tab[T_FLOORS];
    m->rooms = (const ImRoom *)(const void *)tab[T_ROOMS];
    m->doors = (const ImDoor *)(const void *)tab[T_DOORS];
    m->windows = (const ImDoor *)(const void *)tab[T_WINDOWS];
    m->corridors = (const ImRect *)(const void *)tab[T_CORRIDORS];
    m->voids = (const ImRect *)(const void *)tab[T_VOIDS];
    m->lifts = (const ImLift *)(const void *)tab[T_LIFTS];
    m->stairs = (const ImPoint *)(const void *)tab[T_STAIRS];
    m->slots = (const ImSlot *)(const void *)tab[T_SLOTS];
    m->islots = (const ImISlot *)(const void *)tab[T_ISLOTS];
    m->iprops = (const ImIProp *)(const void *)tab[T_IPROPS];
    m->props = (const ImProp *)(const void *)tab[T_PROPS];
    m->site = (const ImSite *)(const void *)tab[T_SITE];
    m->spawns = (const ImSpawn *)(const void *)tab[T_SPAWNS];
    m->learn = (const ImLearn *)(const void *)tab[T_LEARN];
    m->floor_count = (int)count[T_FLOORS];
    m->room_count = (int)count[T_ROOMS];
    m->door_count = (int)count[T_DOORS];
    m->window_count = (int)count[T_WINDOWS];
    m->corridor_count = (int)count[T_CORRIDORS];
    m->void_count = (int)count[T_VOIDS];
    m->lift_count = (int)count[T_LIFTS];
    m->stairs_count = (int)count[T_STAIRS];
    m->slot_count = (int)count[T_SLOTS];
    m->islot_count = (int)count[T_ISLOTS];
    m->iprop_count = (int)count[T_IPROPS];
    m->prop_count = (int)count[T_PROPS];
    m->site_count = (int)count[T_SITE];
    m->spawn_count = (int)count[T_SPAWNS];
    m->learn_count = (int)count[T_LEARN];
    CHECK(m->room_count > 0 && m->room_count < 32768 && m->prop_count < 32768, "imap: table sizes");

    /* cross-references, once */
    for (int i = 0; i < m->floor_count; i++) {
        CHECK(str_ok(m, m->floors[i].name) && m->floors[i].storey >= 0 && m->floors[i].storey <= IMAP_ROOFS,
              "imap: floor %d", i);
    }
    for (int i = 0; i < m->room_count; i++) {
        const ImRoom *r = &m->rooms[i];
        CHECK(str_ok(m, r->id) && str_ok(m, r->name) && str_ok(m, r->type_name) && str_ok(m, r->role) &&
                  r->type < IM_ROOM_TYPE_COUNT && r->storey >= 0 && r->storey < IMAP_ROOFS && r->x0 < r->x1 &&
                  r->z0 < r->z1 && r->chapter >= -1 && r->chapter < 5,
              "imap: room %d", i);
        CHECK(r->door_count == 0 || (r->first_door != IMAP_NONE && (int)r->first_door + r->door_count <= m->door_count),
              "imap: room %s doors", m->strings + r->id);
    }
    const ImDoor *dw[2] = {m->doors, m->windows};
    const int dn[2] = {m->door_count, m->window_count};
    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < dn[k]; i++) {
            const ImDoor *d = &dw[k][i];
            CHECK(d->room >= 0 && d->room < m->room_count && m->rooms[d->room].storey == d->storey && d->w > 0.0f &&
                      (d->side == 'N' || d->side == 'S' || d->side == 'E' || d->side == 'W'),
                  "imap: %s %d", k ? "window" : "door", i);
        }
    }
    for (int i = 0; i < m->corridor_count; i++) {
        CHECK(m->corridors[i].storey >= 0 && m->corridors[i].storey <= IMAP_ROOFS && m->corridors[i].kind <= IM_CORR_DECK,
              "imap: corridor %d", i);
    }
    for (int i = 0; i < m->lift_count; i++) {
        CHECK(str_ok(m, m->lifts[i].name) && (m->lifts[i].landing_mask & 1u), "imap: lift %d", i);
    }
    for (int i = 0; i < m->slot_count; i++) {
        const ImSlot *s = &m->slots[i];
        CHECK(s->room >= 0 && s->room < m->room_count && s->kind < IMS_KIND_COUNT && m->rooms[s->room].storey == s->storey,
              "imap: slot %d", i);
    }
    for (int i = 0; i < m->prop_count; i++) {
        const ImProp *p = &m->props[i];
        CHECK(str_ok(m, p->id) && p->kind < IM_FURN_KIND_COUNT && p->mount <= IM_MOUNT_DOOR && p->storey >= 0 &&
                  p->storey < IMAP_ROOFS && (p->room == -1 || (p->room >= 0 && p->room < m->room_count &&
                                                                  m->rooms[p->room].storey == p->storey)) &&
                  p->w > 0.0f && p->d > 0.0f && p->h >= 0.0f,
              "imap: furniture %d", i);
    }
    for (int i = 0; i < m->iprop_count; i++) {
        const ImIProp *p = &m->iprops[i];
        CHECK(str_ok(m, p->id) && str_ok(m, p->label) && p->kind < IMP_KIND_COUNT && p->room >= 0 &&
                  p->room < m->room_count && m->rooms[p->room].storey == p->storey &&
                  (p->furniture == -1 || (p->furniture >= 0 && p->furniture < m->prop_count)),
              "imap: book prop %d", i);
    }
    for (int i = 0; i < m->islot_count; i++) {
        const ImISlot *s = &m->islots[i];
        CHECK(str_ok(m, s->id) && s->kind <= 1 && s->room >= 0 && s->room < m->room_count &&
                  (s->furniture == -1 || (s->furniture >= 0 && s->furniture < m->prop_count)),
              "imap: NPC point %d", i);
    }
    for (int i = 0; i < m->site_count; i++) {
        CHECK(m->site[i].kind <= IM_SITE_HELIPAD, "imap: site piece %d", i);
    }
    for (int i = 0; i < m->spawn_count; i++) {
        CHECK(m->spawns[i].storey >= -1 && m->spawns[i].storey <= IMAP_ROOFS, "imap: spawn %d", i);
    }
    for (int i = 0; i < m->learn_count; i++) {
        const ImLearn *l = &m->learn[i];
        CHECK(str_ok(m, l->pack) && str_ok(m, l->kind) && str_ok(m, l->topic) && l->room >= 0 && l->room < m->room_count,
              "imap: learn %d", i);
    }
    return true;
}

bool imap_open_embedded(IMap *m)
{
    const MgAsset *a = mg_asset_find("assets/world/institute.mapb");
    if (!a) {
        mg_log_error("imap: assets/world/institute.mapb is not embedded");
        memset(m, 0, sizeof(*m));
        return false;
    }
    return imap_open(m, a->data, a->size);
}

int imap_room_find(const IMap *m, const char *id)
{
    for (int i = 0; i < m->room_count; i++) {
        if (strcmp(imap_str(m, m->rooms[i].id), id) == 0) {
            return i;
        }
    }
    return -1;
}

int imap_room_at(const IMap *m, int storey, float x, float z)
{
    for (int i = 0; i < m->room_count; i++) {
        const ImRoom *r = &m->rooms[i];
        if (r->storey == storey && x >= r->x0 && x < r->x1 && z >= r->z0 && z < r->z1) {
            return i;
        }
    }
    return -1;
}
