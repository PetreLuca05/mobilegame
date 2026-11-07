/* Controller checks against the real dev room colliders (step 3 spec). */
#include <math.h>
#include <stdio.h>

#include "devroom.h"
#include "physics/kinematic.h"

static int g_fail;

#define CHECK(cond, ...)                                                                                     \
    do {                                                                                                     \
        printf("  [%s] ", (cond) ? " ok " : "FAIL");                                                         \
        printf(__VA_ARGS__);                                                                                 \
        printf("\n");                                                                                        \
        if (!(cond)) {                                                                                       \
            g_fail++;                                                                                        \
        }                                                                                                    \
    } while (0)

typedef struct Sim {
    DevRoom room;
    ColWorld world;
    KinematicParams p;
    KinematicState s;
    bool prev_jump;
    float max_y;
} Sim;

static const float DT = 1.0f / 60.0f;

static void sim_init(Sim *sim, float x, float y, float z)
{
    devroom_layout(&sim->room);
    devroom_colliders(&sim->room, &sim->world);
    kinematic_default_params(&sim->p);
    kinematic_init(&sim->s, HMM_V3(x, y, z));
    sim->prev_jump = false;
    sim->max_y = y;
    /* settle onto whatever is below */
    KinematicInput in = {0};
    for (int i = 0; i < 30; i++) {
        kinematic_step(&sim->s, &sim->p, &sim->world, &in, DT);
    }
    sim->max_y = sim->s.pos.Y;
}

static void tick(Sim *sim, float mx, float mz, bool sprint, bool jump)
{
    KinematicInput in = {mx, mz, sprint, jump && !sim->prev_jump, jump};
    sim->prev_jump = jump;
    kinematic_step(&sim->s, &sim->p, &sim->world, &in, DT);
    if (sim->s.pos.Y > sim->max_y) {
        sim->max_y = sim->s.pos.Y;
    }
}

static float hspeed(const Sim *sim) { return sqrtf(sim->s.vel.X * sim->s.vel.X + sim->s.vel.Z * sim->s.vel.Z); }

static void test_accel_stop(void)
{
    Sim sim;
    sim_init(&sim, -6.0f, 0.0f, 9.5f);
    CHECK(sim.s.grounded && fabsf(sim.s.pos.Y) < 0.01f, "spawns grounded on the floor (y=%.3f)", sim.s.pos.Y);
    for (int i = 0; i < 9; i++) {
        tick(&sim, 1, 0, false, false);
    }
    CHECK(fabsf(hspeed(&sim) - 4.5f) < 0.05f, "reaches walk speed 4.5 m/s in 0.15 s (%.2f)", hspeed(&sim));
    for (int i = 0; i < 30; i++) {
        tick(&sim, 1, 0, false, false);
    }
    int ticks = 0;
    while (hspeed(&sim) > 0.001f && ticks < 60) {
        tick(&sim, 0, 0, false, false);
        ticks++;
    }
    CHECK(ticks >= 8 && ticks <= 10, "stops from walk speed in %.3f s (spec 0.15)", ticks * DT);

    sim_init(&sim, -6.0f, 0.0f, 9.5f);
    for (int i = 0; i < 40; i++) {
        tick(&sim, 1, 0, true, false);
    }
    CHECK(fabsf(hspeed(&sim) - 7.0f) < 0.05f, "sprint speed 7 m/s (%.2f)", hspeed(&sim));
    ticks = 0;
    while (hspeed(&sim) > 0.001f && ticks < 60) {
        tick(&sim, 0, 0, false, false);
        ticks++;
    }
    CHECK(ticks >= 8 && ticks <= 10, "stops from sprint in %.3f s (spec 0.15)", ticks * DT);
}

static void test_jump_shape(void)
{
    Sim sim;
    sim_init(&sim, 0.0f, 0.0f, 9.5f);
    float v0 = kinematic_jump_velocity(&sim.p);
    CHECK(fabsf(v0 - 4.85f) < 0.01f, "jump velocity %.3f m/s (spec 4.85)", v0);
    for (int i = 0; i < 90; i++) {
        tick(&sim, 0, 0, false, true);
    }
    CHECK(fabsf(sim.max_y - 1.2f) < 0.02f, "held jump apex %.3f m (spec 1.2)", sim.max_y);
    CHECK(sim.s.grounded, "lands again");

    sim_init(&sim, 0.0f, 0.0f, 9.5f);
    for (int i = 0; i < 4; i++) {
        tick(&sim, 0, 0, false, true);
    }
    for (int i = 0; i < 90; i++) {
        tick(&sim, 0, 0, false, false);
    }
    CHECK(sim.max_y < 0.75f, "tap jump is cut short on release (apex %.3f m)", sim.max_y);
}

/* walk in direction (dx, dz) for n ticks */
static void walk(Sim *sim, float dx, float dz, int n)
{
    for (int i = 0; i < n; i++) {
        tick(sim, dx, dz, false, false);
    }
}

static void test_steps(void)
{
    const float zs[4] = {-9.0f, -6.0f, -3.0f, 0.0f};
    const float hs[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    for (int i = 0; i < 4; i++) {
        Sim sim;
        sim_init(&sim, -5.6f, 0.0f, zs[i]);
        for (int t = 0; t < 120 && sim.s.pos.X > -8.3f; t++) {
            tick(&sim, -1, 0, false, false);
        }
        walk(&sim, 0, 0, 30);
        bool on_top = fabsf(sim.s.pos.Y - hs[i]) < 0.02f && sim.s.pos.X < -7.6f;
        if (i == 0) {
            CHECK(on_top && sim.s.grounded, "climbs the %.2f m step (y=%.3f x=%.2f)", hs[i], sim.s.pos.Y, sim.s.pos.X);
        } else {
            CHECK(!on_top && sim.s.pos.Y < 0.05f && sim.s.pos.X > -7.2f, "blocked by the %.2f m step (y=%.3f x=%.2f)",
                  hs[i], sim.s.pos.Y, sim.s.pos.X);
        }
    }
}

static void jump_at_platform(Sim *sim, float jump_x)
{
    bool jumped = false;
    for (int i = 0; i < 150; i++) {
        bool on_platform = sim->s.grounded && sim->s.pos.Y > 0.5f;
        bool press = !jumped && sim->s.pos.X >= jump_x;
        if (press) {
            jumped = true;
        }
        tick(sim, on_platform ? 0.0f : 1.0f, 0.0f, false, jumped && !on_platform);
    }
}

static void test_platforms(void)
{
    Sim sim;
    sim_init(&sim, 5.6f, 0.0f, -2.0f);
    jump_at_platform(&sim, 6.0f);
    CHECK(fabsf(sim.s.pos.Y - 1.0f) < 0.02f && sim.s.grounded, "walk-speed jump clears the 1.0 m platform (y=%.3f)",
          sim.s.pos.Y);

    sim_init(&sim, 5.6f, 0.0f, -5.5f);
    jump_at_platform(&sim, 6.0f);
    CHECK(sim.s.pos.Y < 0.05f && sim.s.pos.X < 7.5f, "walk-speed jump fails the 1.5 m platform (y=%.3f, apex %.2f)",
          sim.s.pos.Y, sim.max_y);

    /* chain 1.0 -> 1.5 -> 2.0 northwards across 1.5 m gaps: run, jump at the
     * edge, steer back with air control once past the landing's centre */
    sim_init(&sim, 8.5f, 1.05f, -2.0f);
    int landed_on = 0;
    const float targets[2] = {1.5f, 2.0f};
    const float centers[2] = {-5.5f, -9.0f};
    for (int leg = 0; leg < 2; leg++) {
        bool jumped = false;
        for (int i = 0; i < 150; i++) {
            float edge = centers[leg] + 3.5f - 1.0f; /* north edge of the platform we stand on */
            if (!jumped && sim.s.grounded && sim.s.pos.Z <= edge + 0.05f) {
                jumped = true;
            }
            bool done = jumped && sim.s.grounded && fabsf(sim.s.pos.Y - targets[leg]) < 0.05f;
            float mz = -1.0f;
            if (done) {
                mz = 0.0f;
            } else if (jumped && sim.s.pos.Z < centers[leg] + 0.2f) {
                mz = 1.0f; /* past the centre: pull back */
            }
            tick(&sim, 0.0f, mz, false, jumped && !done);
            if (done && hspeed(&sim) < 0.01f) {
                break;
            }
        }
        if (fabsf(sim.s.pos.Y - targets[leg]) < 0.05f && sim.s.grounded) {
            landed_on++;
        }
    }
    CHECK(landed_on == 2, "platform chain 1.0 -> 1.5 -> 2.0 across 1.5 m gaps (landed %d/2, y=%.2f)", landed_on,
          sim.s.pos.Y);
}

static void test_ramps(void)
{
    Sim sim;
    sim_init(&sim, -4.0f, 0.0f, -1.0f);
    for (int i = 0; i < 240 && sim.s.pos.Z > -9.2f; i++) {
        tick(&sim, 0, -1, false, false);
    }
    float rise20 = 6.0f * sinf(HMM_AngleDeg(20.0f));
    CHECK(fabsf(sim.s.pos.Y - rise20) < 0.03f && sim.s.grounded, "walks up the 20 deg ramp to its landing (y=%.3f)",
          sim.s.pos.Y);

    sim_init(&sim, 4.0f, 0.0f, -4.0f);
    walk(&sim, 0, -1, 180);
    CHECK(sim.max_y < 0.5f, "cannot walk up the 50 deg ramp (max y %.2f)", sim.max_y);

    sim_init(&sim, 4.0f, 2.4f, -6.8f);
    walk(&sim, 0, 0, 240);
    CHECK(sim.s.pos.Y < 0.05f && sim.s.pos.Z > -5.6f && sim.s.pos.Z < -2.5f && hspeed(&sim) < 0.01f,
          "slides off the 50 deg ramp and comes to rest at its foot (y=%.2f z=%.2f)", sim.s.pos.Y, sim.s.pos.Z);
}

static void test_corners(void)
{
    Sim sim;
    sim_init(&sim, -11.0f, 0.0f, 9.0f);
    float z0 = sim.s.pos.Z;
    float d = 1.0f / sqrtf(1.25f);
    walk(&sim, -0.5f * d, -1.0f * d, 60);
    CHECK(z0 - sim.s.pos.Z > 3.0f, "slides along the west wall at a 27 deg angle (moved %.2f m in 1 s)",
          z0 - sim.s.pos.Z);

    sim_init(&sim, 0.6f, 0.0f, -1.0f);
    walk(&sim, 0, -1, 120);
    CHECK(sim.s.pos.Z < -5.0f, "slips past the pillar corner without snagging (z=%.2f x=%.2f)", sim.s.pos.Z,
          sim.s.pos.X);

    /* run diagonally into the north-west room corner, then along the wall */
    sim_init(&sim, -10.0f, 0.0f, 9.0f);
    walk(&sim, -0.7071f, 0.7071f, 90);
    walk(&sim, 1, 0, 60);
    CHECK(sim.s.pos.X > -9.0f && sim.s.grounded, "leaves the south-west corner along the wall (x=%.2f)", sim.s.pos.X);
}

static void test_coyote_buffer(void)
{
    /* walk west off the 1.0 m platform and press jump k ticks after leaving */
    const int delays[2] = {3, 9};
    for (int c = 0; c < 2; c++) {
        Sim sim;
        sim_init(&sim, 8.5f, 1.05f, -2.0f);
        int left_at = -1;
        uint32_t jumps0 = sim.s.jump_count;
        for (int i = 0; i < 120; i++) {
            if (left_at < 0 && !sim.s.grounded) {
                left_at = i;
            }
            bool press = left_at >= 0 && i - left_at >= delays[c];
            tick(&sim, -1, 0, false, press);
        }
        bool jumped = sim.s.jump_count > jumps0;
        if (c == 0) {
            CHECK(jumped, "coyote: jump %.2f s after walking off a ledge still works", delays[c] * DT);
        } else {
            CHECK(!jumped, "coyote: jump %.2f s after the ledge is refused", delays[c] * DT);
        }
    }

    Sim sim;
    sim_init(&sim, 0.0f, 0.0f, 9.5f);
    tick(&sim, 0, 0, false, true);
    bool pressed_again = false;
    for (int i = 0; i < 120; i++) {
        bool desc = sim.s.vel.Y < 0.0f && sim.s.pos.Y < 0.35f;
        bool hold = !pressed_again && desc;
        if (hold) {
            pressed_again = true;
        }
        tick(&sim, 0, 0, false, i < 2 || (pressed_again && i < 200 && sim.s.jump_count < 2));
    }
    CHECK(sim.s.jump_count == 2, "jump buffer: pressing just before landing jumps again (jumps=%u)",
          (unsigned)sim.s.jump_count);
}

static void test_long_run(void)
{
    Sim sim;
    sim_init(&sim, 0.0f, 0.0f, 9.0f);
    float min_y = 0.0f;
    for (int i = 0; i < 60 * 20; i++) {
        float a = (float)i * 0.013f;
        tick(&sim, cosf(a), sinf(a * 1.7f), (i / 90) % 2 == 0, (i % 47) < 20);
        if (sim.s.pos.Y < min_y) {
            min_y = sim.s.pos.Y;
        }
    }
    float x = sim.s.pos.X, z = sim.s.pos.Z;
    CHECK(min_y > -0.01f && fabsf(x) < 11.7f && fabsf(z) < 11.7f, "20 s random run stays in the room (min y %.3f)",
          min_y);
}

int test_kinematic(void)
{
    g_fail = 0;
    printf("kinematic controller\n");
    test_accel_stop();
    test_jump_shape();
    test_steps();
    test_platforms();
    test_ramps();
    test_corners();
    test_coyote_buffer();
    test_long_run();
    return g_fail;
}
