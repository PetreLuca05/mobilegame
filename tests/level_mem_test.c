/* Level scenes (per-level scene plumbing): the GPU mesh pool's mark / reset, the level arena carved without touching
 * its pages, the nav bake and search with several lift cars (the path rides the car connected to both ends that makes
 * the walk shortest), a lift with five landings (G 1 2 3 R) and the save's visited-room bits for five storeys.
 * Pure C: brings its own mg_time_now, links without SDL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/arena.h"
#include "core/time.h"
#include "elevator.h"
#include "gfx/mesh_pool.h"
#include "nav/navgrid.h"
#include "physics/collide.h"
#include "save.h"

double mg_time_now(void) { return (double)clock() / (double)CLOCKS_PER_SEC; }

static int failures;

#define CHECK(cond, ...)                                                                                                 \
    do {                                                                                                                 \
        if (!(cond)) {                                                                                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                  \
            printf(__VA_ARGS__);                                                                                         \
            printf("\n");                                                                                                \
            failures++;                                                                                                  \
        }                                                                                                                \
    } while (0)

static void test_mesh_pool(void)
{
    GfxMeshPool p = {.max = 8, .vb_cap = 1000, .ib_cap = 500};
    CHECK(mesh_pool_take(&p, 100, 50) == 0, "first id");
    CHECK(mesh_pool_take(&p, 200, 20) == 1, "second id");
    const GfxMeshMark shared = mesh_pool_mark(&p); /* UI, avatar, map: below the mark */
    for (int level = 0; level < 3; level++) {        /* hospital, institute, hospital: nothing grows */
        const uint32_t n = level == 1 ? 4u : 3u;
        for (uint32_t i = 0; i < n; i++) {
            CHECK(mesh_pool_fits(&p, 150, 60), "room for level mesh %u", i);
            CHECK(mesh_pool_take(&p, 150, 60) == 2 + i, "level ids start at the mark");
        }
        CHECK(p.count == 2 + n && p.vb_used == 300 + 150 * n && p.ib_used == 70 + 60 * n, "level %d usage", level);
        CHECK(mesh_pool_reset_to(&p, shared), "reset to the mark");
        CHECK(p.count == 2 && p.vb_used == 300 && p.ib_used == 70, "back at the mark after level %d", level);
    }
    CHECK(!mesh_pool_fits(&p, 800, 10), "vertex pool full");
    CHECK(!mesh_pool_fits(&p, 10, 450), "index pool full");
    const GfxMeshMark high = {5, 900, 100};
    CHECK(!mesh_pool_reset_to(&p, high), "a stale mark above the top is refused");
    CHECK(p.count == 2 && p.vb_used == 300, "a refused reset changes nothing");
    GfxMeshPool full = {.max = 2, .vb_cap = 1000, .ib_cap = 1000};
    mesh_pool_take(&full, 1, 1);
    mesh_pool_take(&full, 1, 1);
    CHECK(!mesh_pool_fits(&full, 1, 1), "mesh slots full");
}

static void test_level_arena(void)
{
    static uint8_t mem[1 << 16];
    memset(mem, 0xAB, sizeof(mem)); /* reserve must not clear: the pages stay untouched */
    Arena perm, level;
    arena_init(&perm, mem, sizeof(mem));
    CHECK(arena_alloc(&perm, 100, 8) != NULL, "perm alloc");
    CHECK(arena_sub_reserve(&perm, &level, 1 << 15), "reserve");
    CHECK(((uintptr_t)level.base & 15u) == 0, "16-byte aligned");
    CHECK(level.base[100] == 0xAB, "reserved pages are not touched");
    CHECK(perm.used >= (1u << 15) + 100, "perm moved past the child");
    CHECK(!arena_sub_reserve(&perm, &level, 1 << 16), "too big is refused");
    uint8_t *a = arena_alloc(&level, 4000, 16);
    CHECK(a && a[0] == 0 && a[3999] == 0, "an allocation is zeroed when handed out");
    const size_t used = level.used;
    arena_reset(&level);
    CHECK(level.used == 0, "reset");
    uint8_t *b = arena_alloc(&level, 4000, 16);
    CHECK(b == a && b[10] == 0, "the next level reuses the same memory, zeroed");
    CHECK(level.used == used, "same usage after the rebuild");
}

/* Two storeys 20 x 4 m (x -10..10, z 0..4), a lift car at each end; storey 1 is split by a wall at x 0 when `split`. */
static NavWorld *two_lift_world(Arena *a, ColWorld *w, bool split)
{
    colworld_clear(w);
    colworld_add_box(w, HMM_V3(0.0f, -0.1f, 2.0f), HMM_V3(10.0f, 0.1f, 2.0f), 0.0f); /* G floor, top 0 */
    colworld_add_box(w, HMM_V3(0.0f, 3.9f, 2.0f), HMM_V3(10.0f, 0.1f, 2.0f), 0.0f);  /* storey 1 floor, top 4 */
    if (split) {
        colworld_add_box(w, HMM_V3(0.0f, 5.5f, 2.0f), HMM_V3(0.1f, 1.5f, 2.0f), 0.0f);
    }
    NavBakeDesc d;
    memset(&d, 0, sizeof(d));
    d.grid_count = 2;
    d.grids[0] = (NavGridDesc){-10.0f, 0.0f, 80, 16, 0.0f};
    d.grids[1] = (NavGridDesc){-10.0f, 0.0f, 80, 16, 4.0f};
    const float fp[4] = {-10.0f, 0.0f, 10.0f, 4.0f};
    memcpy(d.footprint, fp, sizeof(fp));
    const float a_car[4] = {-9.5f, 1.0f, -7.5f, 3.0f}, b_car[4] = {7.5f, 1.0f, 9.5f, 3.0f};
    memcpy(d.cars[0], a_car, sizeof(a_car));
    memcpy(d.cars[1], b_car, sizeof(b_car));
    d.car_count = 2;
    d.radius = 0.35f;
    d.height = 1.8f;
    d.step = 0.35f;
    return nav_bake(a, w, &d);
}

static void test_nav_two_lifts(void)
{
    static uint8_t mem[8 << 20];
    static ColWorld world;
    Arena a;
    arena_init(&a, mem, sizeof(mem));
    NavWorld *n = two_lift_world(&a, &world, false);
    CHECK(n != NULL, "bake");
    if (!n) {
        return;
    }
    CHECK(nav_flags_at(n, 0, -8.5f, 2.0f) & NAV_ELEVATOR, "car A on G");
    CHECK(nav_flags_at(n, 1, 8.5f, 2.0f) & NAV_ELEVATOR, "car B on 1");
    CHECK(!(nav_flags_at(n, 1, 0.0f, 2.0f) & NAV_ELEVATOR), "no car in the middle");
    NavPath p;
    CHECK(nav_find_path(n, HMM_V3(-6.0f, 0.0f, 2.0f), 0, HMM_V3(-5.0f, 4.0f, 2.0f), 1, &p) == 0 && p.ride_car == 0 && p.ride_at > 0,
          "west to west rides car A (car %d)", p.ride_car);
    CHECK(nav_find_path(n, HMM_V3(5.0f, 0.0f, 2.0f), 0, HMM_V3(6.0f, 4.0f, 2.0f), 1, &p) == 0 && p.ride_car == 1, "east to east rides car B (car %d)",
          p.ride_car);
    CHECK(nav_find_path(n, HMM_V3(-6.0f, 0.0f, 2.0f), 0, HMM_V3(-6.0f, 0.0f, 3.0f), 0, &p) == 0 && p.ride_car == -1 && p.ride_at == -1, "one storey: no ride");
    /* the queued search agrees */
    const uint32_t id = nav_request(n, NAV_CLASS_PLAYER, HMM_V3(6.0f, 0.0f, 2.0f), 0, HMM_V3(5.0f, 4.0f, 2.0f), 1);
    nav_service(n, NAV_CLASS_PLAYER, 0.0, 0, 0);
    bool partial;
    CHECK(nav_poll(n, NAV_CLASS_PLAYER, id, &p, &partial) == NAV_STATUS_READY && p.ride_car == 1 && p.ride_to == 1, "queued: car B (car %d)", p.ride_car);

    /* storey 1 split: only car B reaches its east half, even from next to car A */
    arena_init(&a, mem, sizeof(mem));
    n = two_lift_world(&a, &world, true);
    CHECK(n != NULL, "bake split");
    if (!n) {
        return;
    }
    CHECK(nav_find_path(n, HMM_V3(-8.0f, 0.0f, 2.0f), 0, HMM_V3(2.0f, 4.0f, 2.0f), 1, &p) == 0 && p.ride_car == 1, "the connected car wins (car %d)", p.ride_car);
    CHECK(nav_find_path(n, HMM_V3(8.0f, 0.0f, 2.0f), 0, HMM_V3(-2.0f, 4.0f, 2.0f), 1, &p) == 0 && p.ride_car == 0, "and the other way (car %d)", p.ride_car);
    CHECK(nav_can_reach(n, HMM_V3(-8.0f, 0.0f, 2.0f), 0, HMM_V3(2.0f, 4.0f, 2.0f), 1), "reachable through car B");
}

static void test_five_landings(void)
{
    Elevator e;
    elevator_init(&e, 5, 4.0f, -1.0f, 1.0f, -1.0f, 1.0f, 2.4f);
    CHECK(e.floors == 5, "five landings (%d)", e.floors);
    CHECK(elevator_request(&e, 4), "G -> R");
    float t = 0.0f;
    while (elevator_busy(&e) && t < 10.0f) {
        elevator_tick(&e, 1.0f / 60.0f);
        t += 1.0f / 60.0f;
    }
    CHECK(e.car_floor == 4 && e.car_y > 15.99f && e.car_y < 16.01f, "on the roof deck (floor %d, y %.2f)", e.car_floor, (double)e.car_y);
    CHECK(t > elevator_ride_time(4) - 0.05f && t < elevator_ride_time(4) + 0.05f, "ride %.2f s, want %.2f", (double)t, (double)elevator_ride_time(4));
    CHECK(e.door_open[4] > 0.99f && e.door_open[0] < 0.01f, "the roof doors are open, G shut");
    CHECK(!elevator_request(&e, 5), "no sixth landing");
    elevator_place(&e, 3);
    CHECK(e.car_floor == 3 && e.door_open[3] == 1.0f && e.door_open[4] == 0.0f, "placed on 3");
}

static void test_visited_bits(void)
{
    SaveGame s;
    memset(&s, 0, sizeof(s));
    save_mark_visited(&s, 0, 0);
    save_mark_visited(&s, 2, 15);
    save_mark_visited(&s, 3, 7);
    save_mark_visited(&s, 4, 3); /* the roof deck */
    save_mark_visited(&s, 8, 0);  /* out of range: ignored */
    save_mark_visited(&s, 1, 16); /* out of range: ignored */
    CHECK(s.visited_rooms == ((1ull << 0) | (1ull << 47) | (1ull << 55)), "storeys 0..3 in the low word (%llx)", (unsigned long long)s.visited_rooms);
    CHECK(s.visited_rooms_hi == (1ull << 3), "storey 4 in the high word (%llx)", (unsigned long long)s.visited_rooms_hi);
}

int main(void)
{
    test_mesh_pool();
    test_level_arena();
    test_nav_two_lifts();
    test_five_landings();
    test_visited_bits();
    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("level_mem: all checks passed\n");
    return 0;
}
