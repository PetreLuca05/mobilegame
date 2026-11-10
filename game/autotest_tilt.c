#include "autotest_tilt.h"

#include <math.h>
#include <string.h>

#include "autotest.h"
#include "core/log.h"
#include "frontend.h"
#include "ui/tilt.h"
#include "ui/ui_style.h"

#define FINGER 97

static int failf(TiltTest *t, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(t->why, sizeof(t->why), fmt, ap);
    va_end(ap);
    return -1;
}

static void next(TiltTest *t, double now, int stage)
{
    t->stage = stage, t->sub = 0, t->item = 0, t->at = now;
}

static void target(const AutotestProbe *p, float x, float y)
{
    *p->tilt.debug = true;
    *p->tilt.dx = x, *p->tilt.dy = y;
}

/* the shader's shift for one instance: depth, the lip swing, then clamped so it never crosses the safe rect */
static void shift_of(const Ui *ui, const float r[4], float depth, float lip, float title, float *sx, float *sy)
{
    float x = -ui->tilt_x * depth, y = -ui->tilt_y * depth;
    if (lip > 0.0f) {
        x += -k_ui.tilt_lip_x * lip * ui->tilt_x * title, y += -k_ui.tilt_lip_y * lip * ui->tilt_y * title;
    }
    const float lox = fminf(0.0f, ui->safe[0] - r[0]), loy = fminf(0.0f, ui->safe[1] - r[1]);
    const float hix = fmaxf(0.0f, ui->safe[2] - (r[0] + r[2])), hiy = fmaxf(0.0f, ui->safe[3] - (r[1] + r[3]));
    *sx = fminf(fmaxf(x, lox), hix), *sy = fminf(fmaxf(y, loy), hiy);
}

/* 2 + 9: one reference: its depth from the table and its lip as specified (the shader's clamp included);
 * the shifted face and lip inside the safe rect */
static int check_ref(TiltTest *t, const AutotestProbe *p, int r, const char *name, float want_d)
{
    const Ui *ui = p->ui;
    const UiTest *ut = p->uitest;
    if (!ut->tref[r].ok) {
        return failf(t, "%s: nothing captured", name);
    }
    const float L = ut->tref[r].lip_h, *F = ut->tref[r].face, *Lr = ut->tref[r].lip;
    if (fabsf(ut->tref[r].face_depth - want_d) > 0.01f || fabsf(ut->tref[r].lip_depth - want_d) > 0.01f) {
        return failf(t, "%s: depth %.1f / lip %.1f pt, the table says %.1f", name, (double)ut->tref[r].face_depth, (double)ut->tref[r].lip_depth, (double)want_d);
    }
    float fx, fy, lx, ly, wfx, wfy, wlx, wly;
    shift_of(ui, F, ut->tref[r].face_depth, 0.0f, 1.0f, &fx, &fy);
    shift_of(ui, Lr, ut->tref[r].lip_depth, L, 1.0f, &lx, &ly);
    shift_of(ui, F, want_d, 0.0f, 1.0f, &wfx, &wfy); /* what §2 asks for, the same clamp */
    shift_of(ui, Lr, want_d, L, 1.0f, &wlx, &wly);
    if (fabsf(fx - wfx) > 0.5f || fabsf(fy - wfy) > 0.5f || fabsf(lx - wlx) > 0.5f || fabsf(ly - wly) > 0.5f) {
        return failf(t, "%s at (%.2f, %.2f): face (%.2f, %.2f) lip (%.2f, %.2f), want (%.2f, %.2f) / (%.2f, %.2f)", name, (double)ui->tilt_x, (double)ui->tilt_y,
                     (double)fx, (double)fy, (double)lx, (double)ly, (double)wfx, (double)wfy, (double)wlx, (double)wly);
    }
    const float x0 = fminf(F[0] + fx, Lr[0] + lx), y0 = fminf(F[1] + fy, Lr[1] + ly);
    const float x1 = fmaxf(F[0] + fx + F[2], Lr[0] + lx + Lr[2]), y1 = fmaxf(F[1] + fy + F[3], Lr[1] + ly + Lr[3]);
    const bool was_in = F[0] >= ui->safe[0] && F[1] >= ui->safe[1] && Lr[0] + Lr[2] <= ui->safe[2] && Lr[1] + Lr[3] <= ui->safe[3];
    if (was_in && (x0 < ui->safe[0] - 0.01f || y0 < ui->safe[1] - 0.01f || x1 > ui->safe[2] + 0.01f || y1 > ui->safe[3] + 0.01f)) {
        return failf(t, "%s at (%.2f, %.2f) leaves the safe rect: (%.1f, %.1f)-(%.1f, %.1f)", name, (double)ui->tilt_x, (double)ui->tilt_y, (double)x0, (double)y0,
                     (double)x1, (double)y1);
    }
    t->unclamped += fabsf(fx + ui->tilt_x * want_d) < 0.01f && fabsf(fy + ui->tilt_y * want_d) < 0.01f;
    return 0;
}

/* pure maths on a private Tilt: steps, noise, the neutral pose, both rotations, the strength rules */
static int check_math(TiltTest *t)
{
    const float dt = 1.0f / 60.0f;
    Tilt a;
    /* 4: right edge 10 deg away, as Core Motion reports it in each rotation */
    float sx1, sy1, sx2, sy2;
    const float s10 = sinf(10.0f * 0.0174533f), c10 = cosf(10.0f * 0.0174533f);
    tilt_screen_gravity(0.0f, -s10, -c10, false, &sx1, &sy1); /* the device's long axis runs one way ... */
    tilt_screen_gravity(0.0f, s10, -c10, true, &sx2, &sy2);   /* ... and the other way when flipped */
    float txs[2];
    for (int k = 0; k < 2; k++) {
        tilt_init(&a);
        tilt_snap(&a, 0.0f, 0.0f);
        for (int i = 0; i < 60; i++) {
            tilt_tick(&a, dt, true, k ? sx2 : sx1, k ? sy2 : sy1, i * dt);
        }
        txs[k] = a.tx;
    }
    if (fabsf(txs[0] - txs[1]) > 1e-4f || fabsf(txs[0]) < 0.3f) {
        return failf(t, "the same tilt in both landscape rotations gave tx %.3f and %.3f", (double)txs[0], (double)txs[1]);
    }
    /* 5: a constant tilt (10 deg) held 12 s */
    tilt_init(&a);
    tilt_snap(&a, 0.0f, 0.0f);
    float first = 0.0f;
    for (int i = 0; i < 12 * 60; i++) {
        tilt_tick(&a, dt, true, s10, 0.0f, i * dt);
        if (i == 30) {
            first = fabsf(a.tx);
        }
    }
    if (fabsf(a.tx) > 0.05f * first) {
        return failf(t, "after 12 s at a constant tilt the offset is %.1f %% of its start (max 5 %%)", (double)(100.0f * fabsf(a.tx) / first));
    }
    const float held = 100.0f * fabsf(a.tx) / first;
    /* 6: +-0.5 deg noise at 60 Hz: under 0.3 pt anywhere (the nearest layer is 14 pt) */
    tilt_init(&a);
    tilt_snap(&a, 0.0f, 0.0f);
    float worst = 0.0f;
    uint32_t rng = 12345u;
    for (int i = 0; i < 10 * 60; i++) {
        rng = rng * 1664525u + 1013904223u;
        const float n = ((float)(rng >> 8) / 16777216.0f * 2.0f - 1.0f) * 0.5f * 0.0174533f;
        rng = rng * 1664525u + 1013904223u;
        const float m = ((float)(rng >> 8) / 16777216.0f * 2.0f - 1.0f) * 0.5f * 0.0174533f;
        tilt_tick(&a, dt, true, sinf(n), sinf(m), i * dt);
        worst = fmaxf(worst, fmaxf(fabsf(a.tx), fabsf(a.ty)) * k_ui.tilt_depth[4]);
    }
    if (worst >= 0.3f) {
        return failf(t, "+-0.5 deg noise moved the nearest layer %.3f pt (max 0.3)", (double)worst);
    }
    /* 7: a step to 1: one overshoot, settled within 600 ms */
    tilt_init(&a);
    a.forced = true;
    a.target_x = 1.0f;
    float peak = 0.0f;
    int overs = 0;
    bool above = false;
    float at600 = 0.0f;
    for (int i = 0; i < 90; i++) {
        tilt_tick(&a, dt, false, 0.0f, 0.0f, i * dt);
        peak = fmaxf(peak, a.tx);
        const bool now_above = a.tx > 1.01f;
        overs += now_above && !above;
        above = now_above;
        if (i == 35) { /* 600 ms */
            at600 = a.tx;
        }
    }
    if (peak <= 1.0f || overs != 1 || fabsf(at600 - 1.0f) > 0.03f) {
        return failf(t, "step: peak %.3f, %d overshoots above 1 %%, %.3f at 600 ms", (double)peak, overs, (double)at600);
    }
    /* 11 */
    const float off = tilt_strength(0, false, false), red = tilt_strength(2, true, false), low = tilt_strength(2, false, true);
    if (off != 0.0f || red != 0.0f || low > 0.5f) {
        return failf(t, "strength: Oprit %.2f, reduced %.2f, Low Power %.2f", (double)off, (double)red, (double)low);
    }
    mg_log_info("autotest tilt: both rotations tx %.3f / %.3f; held 12 s -> %.1f %%; noise -> %.3f pt; step peak %.3f, 1 overshoot, %.3f at 600 ms; S Oprit %.1f, reduced %.1f, Low Power %.1f",
                (double)txs[0], (double)txs[1], (double)held, (double)worst, (double)peak, (double)at600, (double)off, (double)red, (double)low);
    return 0;
}

enum { S_START, S_ZERO, S_EXTREMES, S_FREEZE, S_SHAKE, S_MENU, S_DONE };

int tt_run(TiltTest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double now)
{
    UiTest *ut = p->uitest;
    const Ui *ui = p->ui;
    static const float k_dir[4][2] = {{1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f}};
    static const char *const k_name[4] = {"right", "left", "down", "up"};
    switch (t->stage) {
    case S_START:
        if (t->at == 0.0) {
            t->at = now;
            target(p, 0.0f, 0.0f);
            *p->tilt.setting = 2;
            if (check_math(t) < 0) {
                return -1;
            }
        }
        if (now - t->at < 2.0 || !p->fe || p->fe->state != FE_MAIN_MENU) {
            return now - t->at > 30.0 ? failf(t, "never reached the menu") : 0;
        }
        ut->screen = true;
        next(t, now, S_ZERO);
        return 0;
    case S_ZERO: { /* 1 */
        static float face_on[3][4];
        if (t->sub == 0 && now - t->at > 1.5) {
            if (ui->tilt_x != 0.0f || ui->tilt_y != 0.0f) {
                return failf(t, "tilt (0, 0) reaches the shader as (%.3f, %.3f)", (double)ui->tilt_x, (double)ui->tilt_y);
            }
            for (int r = 0; r < 3; r++) {
                memcpy(face_on[r], ut->tref[r].face, sizeof(face_on[r]));
            }
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "tilt_zero_on");
            *p->tilt.setting = 0;
            t->sub = 1, t->at = now;
        } else if (t->sub == 1 && now - t->at > 0.6) {
            for (int r = 0; r < 3; r++) {
                if (memcmp(face_on[r], ut->tref[r].face, sizeof(face_on[r])) != 0) {
                    return failf(t, "tilt (0, 0): reference %d differs with the setting off", r);
                }
            }
            if (*p->tilt.s != 0.0f) {
                return failf(t, "Oprit: S = %.2f", (double)*p->tilt.s);
            }
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "tilt_zero_off");
            *p->tilt.setting = 2;
            mg_log_info("autotest tilt: tilt (0, 0): the same instances with the setting on and off, zero tilt in the shader");
            next(t, now, S_EXTREMES);
        }
        return 0;
    }
    case S_EXTREMES: /* 2, 3, 9 */
        if (t->item >= 4) {
            target(p, 0.0f, 0.0f);
            next(t, now, S_FREEZE);
            return 0;
        }
        if (t->sub == 0) {
            target(p, k_dir[t->item][0], k_dir[t->item][1]);
            t->sub = 1, t->at = now;
        } else if (t->sub == 1 && now - t->at > 1.2) {
            static const char *const names[3] = {"card", "button", "badge"};
            static const int level[3] = {1, 2, 3};
            for (int r = 0; r < 3; r++) {
                if (check_ref(t, p, r, names[r], k_ui.tilt_depth[level[r]]) < 0) {
                    return -1;
                }
            }
            t->fired0 = ut->fired[0];
            autotest_finger(app, SDL_EVENT_FINGER_DOWN, FINGER, ut->bx[0], ut->by[0]); /* the rest centre */
            t->sub = 2, t->at = now;
        } else if (t->sub == 2 && now - t->at > 0.08) {
            autotest_finger(app, SDL_EVENT_FINGER_UP, FINGER, ut->bx[0], ut->by[0]);
            t->sub = 3, t->at = now;
        } else if (t->sub == 3 && now - t->at > 0.2) {
            if (ut->fired[0] != t->fired0 + 1) {
                return failf(t, "tilt %s: a tap on the button's rest centre did not fire", k_name[t->item]);
            }
            mg_log_info("autotest tilt: %s: card 4 / button 7 / badge 10 pt and the lips as specified, inside the safe rect (%d of 3 needed no clamp); a tap on the rest centre fires",
                        k_name[t->item], t->unclamped);
            t->unclamped = 0;
            t->item++, t->sub = 0;
        }
        return 0;
    case S_FREEZE: /* 10: hold the button, swing the tilt: its shift stays */
        if (t->sub == 0 && now - t->at > 0.3) {
            target(p, 1.0f, 0.0f);
            t->sub = 1, t->at = now;
        } else if (t->sub == 1 && now - t->at > 1.2) {
            autotest_finger(app, SDL_EVENT_FINGER_DOWN, FINGER, ut->bx[0], ut->by[0]);
            t->sub = 2, t->at = now;
        } else if (t->sub == 2 && now - t->at > 0.15) {
            t->held_x = ut->tref[1].face[0] - ut->tref[1].face_depth * ui->tilt_x; /* where it is drawn */
            target(p, -1.0f, 0.0f);
            t->sub = 3, t->at = now;
        } else if (t->sub == 3 && now - t->at > 1.0) {
            const float x = ut->tref[1].face[0] - ut->tref[1].face_depth * ui->tilt_x;
            autotest_finger(app, SDL_EVENT_FINGER_UP, FINGER, ut->bx[0], ut->by[0]);
            if (fabsf(x - t->held_x) > 0.05f) {
                return failf(t, "a held button slid %.2f pt while the tilt changed", (double)(x - t->held_x));
            }
            mg_log_info("autotest tilt: a held button keeps its shift (moved %.3f pt while the tilt went from +1 to -1)", (double)(x - t->held_x));
            target(p, 0.0f, 0.0f);
            next(t, now, S_SHAKE);
        }
        return 0;
    case S_SHAKE: /* 8 */
        if (t->sub == 0 && now - t->at > 2.5) { /* past the last shake's 2 s */
            t->jig0 = ui->jiggles_started;
            target(p, 1.0f, 0.0f); /* a step: the spring's speed passes 6 / s */
            t->sub = 1, t->at = now;
        } else if (t->sub == 1 && now - t->at > 0.6) {
            const int first = ui->jiggles_started - t->jig0;
            if (first < 3) {
                return failf(t, "a shake started %d wobbles", first);
            }
            t->jig0 = ui->jiggles_started;
            t->item = first;
            target(p, -1.0f, 0.0f); /* again, within 2 s */
            t->sub = 2, t->at = now;
        } else if (t->sub == 2 && now - t->at > 0.8) {
            if (ui->jiggles_started != t->jig0) {
                return failf(t, "a second shake within 2 s wobbled %d more", ui->jiggles_started - t->jig0);
            }
            mg_log_info("autotest tilt: shake: %d staggered wobbles; a second shake within 2 s: none", t->item);
            target(p, 0.0f, 0.0f);
            ut->screen = false;
            next(t, now, S_MENU);
        }
        return 0;
    case S_MENU: /* 3 (hit rects) + the four extreme screenshots of the main menu */
        if (t->sub == 0 && now - t->at > 2.5) {
            t->rest_n = 0;
            for (int i = 0; i < ui->seen_count && t->rest_n < 16; i++) {
                if (!ui->seen[i].rect_only) {
                    t->rest_key[t->rest_n] = ui->seen[i].key;
                    t->rest[t->rest_n][0] = ui->seen[i].x, t->rest[t->rest_n][1] = ui->seen[i].y;
                    t->rest[t->rest_n][2] = ui->seen[i].w, t->rest[t->rest_n][3] = ui->seen[i].h;
                    t->rest_n++;
                }
            }
            t->sub = 1, t->item = 0, t->at = now;
            return 0;
        }
        if (t->sub == 1) {
            if (t->item >= 4) {
                target(p, 0.0f, 0.0f);
                next(t, now, S_DONE);
                return 0;
            }
            target(p, k_dir[t->item][0], k_dir[t->item][1]);
            t->sub = 2, t->at = now;
        } else if (t->sub == 2 && now - t->at > 1.5) {
            for (int k = 0; k < t->rest_n; k++) {
                for (int i = 0; i < ui->seen_count; i++) {
                    if (ui->seen[i].key == t->rest_key[k] &&
                        (ui->seen[i].x != t->rest[k][0] || ui->seen[i].y != t->rest[k][1] || ui->seen[i].w != t->rest[k][2] || ui->seen[i].h != t->rest[k][3])) {
                        return failf(t, "menu, tilt %s: a hit rect moved", k_name[t->item]);
                    }
                }
            }
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "tilt_menu_%s", k_name[t->item]);
            mg_log_info("autotest tilt: menu, tilt %s: %d hit rects at rest; screenshot", k_name[t->item], t->rest_n);
            t->item++, t->sub = 1;
        }
        return 0;
    default:
        *p->tilt.debug = false;
        return now - t->at > 0.5 ? 1 : 0;
    }
}
