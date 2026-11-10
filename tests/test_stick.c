/* UI-1b: the stick's input mapping must never change. A scripted touch path is fed to
 * touch_controls_update (no SDL, no frame timing) for the iPhone and iPad configs, and every
 * tick's outputs are compared with tests/stick_golden.txt, recorded before the new look.
 *   mg_tests                    compare
 *   mg_tests --record-stick F   write the recording to F */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui/touch_controls.h"

/* the drawing side is not under test */
void ui_circle(GfxContext *g, const TextFont *f, float cx, float cy, float r, UiColor c)
{
    (void)g, (void)f, (void)cx, (void)cy, (void)r, (void)c;
}
void text_draw_centered(GfxContext *g, const TextFont *f, float cx, float cy, float size_pt, UiColor c, const char *text)
{
    (void)g, (void)f, (void)cx, (void)cy, (void)size_pt, (void)c, (void)text;
}

#define W 844.0f
#define H 390.0f
#define TICK (1.0 / 60.0)

typedef struct Sim {
    MgInput in;
    TouchControls tc;
    TouchControlsConfig cfg;
    double now;
    FILE *out;  /* recording */
    FILE *gold; /* comparing */
    int line, mismatches;
} Sim;

static void emit(Sim *s, const char *tag)
{
    char buf[256];
    const TouchControls *t = &s->tc;
    snprintf(buf, sizeof(buf), "%s %d %.6f %.6f %d %.4f %.4f %d %u\n", tag, t->stick_touch, (double)t->move_x, (double)t->move_y, t->sprint ? 1 : 0,
             (double)t->knob_dx, (double)t->knob_dy, t->pending_touch, (unsigned)t->tapped_id);
    s->line++;
    if (s->out) {
        fputs(buf, s->out);
    } else if (s->gold) {
        char want[256];
        if (!fgets(want, sizeof(want), s->gold) || strcmp(want, buf) != 0) {
            if (s->mismatches++ < 5) {
                printf("  [FAIL] stick golden line %d: got %s", s->line, buf);
            }
        }
    }
}

/* one tick: the input layer's flags, then the update */
static void tick(Sim *s, const char *tag)
{
    touch_controls_update(&s->tc, &s->cfg, &s->in, W, H, 0.0f, 0.0f, s->now);
    emit(s, tag);
    for (int i = 0; i < MG_MAX_TOUCHES; i++) { /* what input_touch does at the next frame's start */
        MgTouch *t = &s->in.touches[i];
        t->began = false;
        t->dx = t->dy = 0.0f;
        if (t->ended) {
            t->ended = false;
            t->owner = MG_TOUCH_FREE;
        }
    }
    s->now += TICK;
}

static void down(Sim *s, int slot, float x, float y)
{
    MgTouch *t = &s->in.touches[slot];
    *t = (MgTouch){.id = slot + 1, .active = true, .began = true, .x = x, .y = y, .x0 = x, .y0 = y, .owner = MG_TOUCH_FREE};
}

static void move(Sim *s, int slot, float x, float y)
{
    MgTouch *t = &s->in.touches[slot];
    t->dx = x - t->x, t->dy = y - t->y;
    t->x = x, t->y = y;
}

static void up(Sim *s, int slot)
{
    s->in.touches[slot].active = false;
    s->in.touches[slot].ended = true;
}

/* the path: every direction at several lengths (dead zone, ramp, plateau, past the rim), a slow spiral,
 * a marker tap, a marker press that turns into the stick, a second finger in the stick zone, the camera side */
static void script(Sim *s)
{
    const float cx = 160.0f, cy = 280.0f;
    for (int dir = 0; dir < 16; dir++) {
        const float a = (float)dir * 0.3926991f; /* 22.5 deg */
        down(s, 0, cx, cy);
        tick(s, "dn");
        static const float lens[] = {5.0f, 20.0f, 31.0f, 40.0f, 45.0f, 52.0f, 57.0f, 60.0f, 75.0f, 120.0f};
        for (size_t k = 0; k < sizeof(lens) / sizeof(lens[0]); k++) {
            move(s, 0, cx + cosf(a) * lens[k], cy + sinf(a) * lens[k]);
            tick(s, "mv");
        }
        up(s, 0);
        tick(s, "up");
        tick(s, "idle");
    }
    down(s, 0, 120.0f, 300.0f); /* a spiral out and back in */
    tick(s, "dn");
    for (int i = 0; i < 240; i++) {
        const float r = 80.0f * sinf((float)i * 0.0131f), a = (float)i * 0.11f;
        move(s, 0, 120.0f + cosf(a) * r, 300.0f + sinf(a) * r);
        tick(s, "sp");
    }
    up(s, 0);
    tick(s, "up");
    /* marker target: a quick tap is the marker's; a press that moves becomes the stick at its start */
    s->tc.target_count = 1;
    s->tc.targets[0].id = 42;
    s->tc.targets[0].x = 200.0f, s->tc.targets[0].y = 250.0f, s->tc.targets[0].half = 22.0f;
    down(s, 1, 205.0f, 252.0f);
    tick(s, "mk");
    tick(s, "mk");
    up(s, 1);
    tick(s, "mk_up");
    down(s, 1, 198.0f, 248.0f);
    tick(s, "mk2");
    for (int i = 1; i <= 8; i++) {
        move(s, 1, 198.0f + 6.0f * (float)i, 248.0f - 2.0f * (float)i);
        tick(s, "mk2_mv");
    }
    up(s, 1);
    tick(s, "mk2_up");
    down(s, 1, 202.0f, 251.0f); /* held still past 250 ms: still pending, then a late move */
    for (int i = 0; i < 20; i++) {
        tick(s, "mk3");
    }
    move(s, 1, 240.0f, 251.0f);
    tick(s, "mk3_mv");
    up(s, 1);
    tick(s, "mk3_up");
    s->tc.target_count = 0;
    /* two fingers on the left: the first keeps the stick; the camera side takes nothing (top-down) */
    down(s, 0, 100.0f, 300.0f);
    tick(s, "two");
    down(s, 2, 150.0f, 200.0f);
    move(s, 0, 140.0f, 300.0f);
    tick(s, "two");
    down(s, 3, 600.0f, 200.0f);
    move(s, 3, 650.0f, 210.0f);
    tick(s, "cam");
    up(s, 0);
    tick(s, "two_up");
    move(s, 2, 210.0f, 200.0f);
    tick(s, "two_after");
    up(s, 2);
    up(s, 3);
    tick(s, "end");
}

static void run(Sim *s, float k, float dead)
{
    memset(&s->in, 0, sizeof(s->in));
    for (int i = 0; i < MG_MAX_TOUCHES; i++) {
        s->in.touches[i].owner = MG_TOUCH_FREE;
    }
    touch_controls_default_config(&s->cfg);
    touch_controls_reset(&s->tc);
    s->tc.target_count = 0;
    s->cfg.radius_pt = 60.0f * k; /* as game.c sets them */
    s->cfg.dead_zone_pt = dead * k;
    s->cfg.jump_size_pt = 0.0f;
    s->now = 100.0;
    char tag[32];
    snprintf(tag, sizeof(tag), "cfg %.2f %.1f", (double)k, (double)dead);
    emit(s, tag);
    script(s);
}

static void all(Sim *s)
{
    run(s, 1.0f, 30.0f);  /* iPhone, default dead zone */
    run(s, 1.4f, 30.0f);  /* iPad */
    run(s, 1.0f, 10.0f);  /* the dead-zone setting's ends */
    run(s, 1.0f, 50.0f);
}

int test_stick_record(const char *path)
{
    Sim s = {0};
    s.out = fopen(path, "w");
    if (!s.out) {
        return 1;
    }
    all(&s);
    fclose(s.out);
    printf("stick golden: %d lines -> %s\n", s.line, path);
    return 0;
}

int test_stick(void)
{
    int failures = 0;
    printf("stick input golden\n");
    Sim s = {0};
    s.gold = fopen(MG_STICK_GOLDEN, "r");
    if (!s.gold) {
        printf("  [FAIL] no %s\n", MG_STICK_GOLDEN);
        return 1;
    }
    all(&s);
    char extra[8];
    if (fgets(extra, sizeof(extra), s.gold)) {
        s.mismatches++;
        printf("  [FAIL] the recording has more lines than the run\n");
    }
    fclose(s.gold);
    printf("  [%s] %d ticks identical to the recording (%d mismatches)\n", s.mismatches ? "FAIL" : "ok", s.line, s.mismatches);
    failures += s.mismatches ? 1 : 0;
    return failures;
}
