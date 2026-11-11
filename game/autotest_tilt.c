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

/* A1 maths on a private Tilt: the angle (deg) about the screen's vertical axis as a function of time, run at 60 Hz.
 * Offsets are the nearest layer's (14 pt). Fills peak / when / the trace for the report. */
typedef float (*AngleFn)(float t);
typedef struct Run {
    float peak, peak_t, max_abs_after, mean, rest_t;
    int overshoots, ceilings, backstops;
    float trace[8];   /* offset (pt) at 0, 0.1, 0.25, 0.5, 0.75, 1.2, 1.5, 2.0 s */
} Run;

static float g_amp, g_end; /* the shapes' parameters */
static float step_hold(float t) { (void)t; return g_amp; }
static float ramp(float t) { return t < g_end ? g_amp * t / g_end : g_amp; }
static float sine(float t) { return t < g_end ? g_amp * sinf(6.2831853f * t) : 0.0f; }

static Run run_shape(AngleFn f, float seconds, float still_from, bool flipped, float noise_deg)
{
    const float dt = 1.0f / 60.0f;
    static const float marks[8] = {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 1.2f, 1.5f, 2.0f};
    Run r = {0};
    r.rest_t = -1.0f;
    Tilt a;
    tilt_init(&a);
    tilt_snap(&a);
    uint32_t rng = 777u;
    float sum = 0.0f;
    int n = 0, sign = 0;
    const float lead = 0.25f; /* level and still first: the reference settles on 0 deg before the shape starts */
    for (int i = 0; i * dt < seconds + lead; i++) {
        const float t = (float)i * dt - lead;
        float deg = t < 0.0f ? 0.0f : f(t);
        if (noise_deg > 0.0f) {
            rng = rng * 1664525u + 1013904223u;
            deg += ((float)(rng >> 8) / 16777216.0f * 2.0f - 1.0f) * noise_deg;
        }
        const float rad = deg * 0.0174533f;
        /* as Core Motion would report it in this rotation, through the game's own mapping */
        float sx, sy;
        tilt_screen_gravity(0.0f, flipped ? sinf(rad) : -sinf(rad), -cosf(rad), flipped, &sx, &sy);
        tilt_tick(&a, dt, true, sx, sy, -1.0f, (double)t + 10.0);
        if (t < 0.0f) {
            continue;
        }
        const float off = a.tx * k_ui.tilt_depth[4];
        if (fabsf(a.tx) > fabsf(r.peak)) {
            r.peak = a.tx, r.peak_t = t;
        }
        if (t >= still_from) {
            r.max_abs_after = fmaxf(r.max_abs_after, fabsf(a.tx));
            const int sg = a.tx > 0.0f ? 1 : a.tx < 0.0f ? -1 : 0;
            if (sg && sign && sg != sign) {
                r.overshoots++;
            }
            if (sg) {
                sign = sg;
            }
            if (r.rest_t < 0.0f && a.tx == 0.0f && a.vx == 0.0f) {
                r.rest_t = t - still_from;
            }
            if (a.tx != 0.0f) {
                r.rest_t = -1.0f; /* rest means staying there */
            }
        } else if (t >= 1.0f) { /* the mean over whole cycles after the first (its start-up is not drift) */
            sum += off, n++;
        }
        for (int m = 0; m < 8; m++) {
            if (fabsf(t - marks[m]) < dt * 0.5f) {
                r.trace[m] = off;
            }
        }
    }
    r.mean = n ? sum / (float)n : 0.0f;
    r.ceilings = a.ceilings, r.backstops = a.backstops;
    return r;
}

static int check_math(TiltTest *t)
{
    /* Hold: 0 -> 20 deg at once, held */
    g_amp = 20.0f;
    const Run hold = run_shape(step_hold, 2.5f, 0.0f, false, 0.0f);
    const float hp = fabsf(hold.peak) * k_ui.tilt_depth[4];
    if (hold.peak_t > 0.25f || fabsf(hold.trace[5]) > 0.1f * hp || hold.trace[7] != 0.0f) {
        return failf(t, "hold: peak %.2f pt at %.0f ms, %.2f pt at 1.2 s, %.3f pt at 2 s", (double)hp, (double)(hold.peak_t * 1000.0f), (double)hold.trace[5],
                     (double)hold.trace[7]);
    }
    mg_log_info("autotest tilt: hold 20 deg, offset (pt) at 0 / 0.1 / 0.25 / 0.5 / 0.75 / 1.2 / 1.5 / 2.0 s: %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f (peak %.2f at %.0f ms)",
                (double)hold.trace[0], (double)hold.trace[1], (double)hold.trace[2], (double)hold.trace[3], (double)hold.trace[4], (double)hold.trace[5],
                (double)hold.trace[6], (double)hold.trace[7], (double)hp, (double)(hold.peak_t * 1000.0f));
    /* still at an angle: 10, 30, 60 deg held 3 s: exactly 0 from second 2 */
    static const float angles[3] = {10.0f, 30.0f, 60.0f};
    for (int k = 0; k < 3; k++) {
        g_amp = angles[k];
        const Run st = run_shape(step_hold, 3.0f, 2.0f, false, 0.0f);
        if (st.max_abs_after != 0.0f) {
            return failf(t, "held still at %.0f deg: offset %.4f after 2 s (want exactly 0)", (double)angles[k], (double)(st.max_abs_after * k_ui.tilt_depth[4]));
        }
    }
    /* during the tilt: a 2 s ramp to 20 deg (10 deg/s): under 35 %, rest within 500 ms of the end */
    g_amp = 20.0f, g_end = 2.0f;
    const Run rp = run_shape(ramp, 3.0f, 2.0f, false, 0.0f);
    if (fabsf(rp.peak) >= 0.35f || rp.rest_t < 0.0f || rp.rest_t > 0.5f || rp.ceilings || rp.backstops) {
        return failf(t, "ramp: peak %.3f (max 0.35), rest %.0f ms after the end (max 500), ceiling %d, backstop %d (a real turn must trip neither)",
                     (double)rp.peak, (double)(rp.rest_t * 1000.0f), rp.ceilings, rp.backstops);
    }
    /* quick flick: 20 deg in 0.3 s, then still */
    g_amp = 20.0f, g_end = 0.3f;
    const Run fl = run_shape(ramp, 2.0f, 0.3f, false, 0.0f);
    const float back = 0.0f; /* how far it went past rest (the other sign) */
    (void)back;
    if (fabsf(fl.peak) < 0.55f || fabsf(fl.peak) > 0.85f || fl.overshoots > 1 || fl.rest_t < 0.0f || fl.rest_t > 0.5f) {
        return failf(t, "flick: peak %.3f (0.55..0.85), %d overshoots, rest %.0f ms after going still", (double)fl.peak, fl.overshoots, (double)(fl.rest_t * 1000.0f));
    }
    /* how far past rest: the most negative value after going still */
    {
        Tilt a;
        tilt_init(&a);
        tilt_snap(&a);
        float under = 0.0f;
        for (int i = 0; i < 135; i++) {
            const float tt = (float)i / 60.0f - 0.25f, rad = (tt < 0.0f ? 0.0f : ramp(tt)) * 0.0174533f;
            float sx, sy;
            tilt_screen_gravity(0.0f, -sinf(rad), -cosf(rad), false, &sx, &sy);
            tilt_tick(&a, 1.0f / 60.0f, true, sx, sy, -1.0f, tt);
            under = fminf(under, a.tx);
        }
        if (-under > 0.1f * fabsf(fl.peak)) {
            return failf(t, "flick: passed rest by %.1f %% of the peak (max 10)", (double)(-under / fabsf(fl.peak) * 100.0f));
        }
        mg_log_info("autotest tilt: flick 20 deg / 0.3 s, offset (pt) at 0 / 0.1 / 0.25 / 0.5 / 0.75 / 1.2 / 1.5 / 2.0 s: %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f (peak %.3f at %.0f ms, past rest %.1f %%, at rest %.0f ms after going still)",
                    (double)fl.trace[0], (double)fl.trace[1], (double)fl.trace[2], (double)fl.trace[3], (double)fl.trace[4], (double)fl.trace[5], (double)fl.trace[6],
                    (double)fl.trace[7], (double)fl.peak, (double)(fl.peak_t * 1000.0f), (double)(-under / fabsf(fl.peak) * 100.0f), (double)(fl.rest_t * 1000.0f));
    }
    /* both rotations: the same flick gives the same tilt */
    const Run fl2 = run_shape(ramp, 2.0f, 0.3f, true, 0.0f);
    if (fabsf(fl.peak - fl2.peak) > 1e-4f) {
        return failf(t, "the same flick in both landscape rotations: peak %.3f / %.3f", (double)fl.peak, (double)fl2.peak);
    }
    /* back and forth: a 1 Hz +-15 deg sine for 5 s */
    g_amp = 15.0f, g_end = 5.0f;
    const Run sn = run_shape(sine, 6.0f, 5.0f, false, 0.0f);
    if (fabsf(sn.mean) > 0.3f || sn.rest_t < 0.0f || sn.rest_t > 0.5f) {
        return failf(t, "sine: mean offset %.2f pt (max 0.3), rest %.0f ms after stopping", (double)sn.mean, (double)(sn.rest_t * 1000.0f));
    }
    /* noise: +-0.5 deg moves nothing more than 0.3 pt */
    g_amp = 0.0f;
    const Run nz = run_shape(step_hold, 10.0f, 10.0f, false, 0.5f);
    if (fabsf(nz.peak) * k_ui.tilt_depth[4] >= 0.3f) {
        return failf(t, "+-0.5 deg noise moved the nearest layer %.3f pt", (double)(fabsf(nz.peak) * k_ui.tilt_depth[4]));
    }
    /* the ceiling: a stuck target with the sensor still */
    {
        Tilt a;
        tilt_init(&a);
        tilt_snap(&a);
        a.fault = true, a.fault_x = 1.0f;
        float zero_at = -1.0f;
        for (int i = 0; i < 180; i++) {
            float sx, sy;
            tilt_screen_gravity(0.0f, 0.0f, -1.0f, false, &sx, &sy);
            tilt_tick(&a, 1.0f / 60.0f, true, sx, sy, 0.0f, i / 60.0);
            if (zero_at < 0.0f && a.tx == 0.0f && i > 10) {
                zero_at = (float)i / 60.0f;
            }
        }
        if (zero_at < 1.5f || zero_at > 1.9f || a.ceilings != 1 || !a.ceiling_logged) { /* the 1.5 s ceiling, then the spring */
            return failf(t, "ceiling: back to 0 at %.2f s (want 1.5..1.9), fired %d times", (double)zero_at, a.ceilings);
        }
        mg_log_info("autotest tilt: ceiling: a stuck target with the sensor still is back to 0 at %.2f s, logged once", (double)zero_at);
    }
    /* the backstop: a stuck target while the sensor says the device keeps turning: 0 after 4 s, logged once */
    {
        Tilt a;
        tilt_init(&a);
        tilt_snap(&a);
        a.fault = true, a.fault_x = 1.0f;
        float zero_at = -1.0f;
        for (int i = 0; i < 6 * 60; i++) {
            float sx, sy;
            tilt_screen_gravity(0.0f, 0.0f, -1.0f, false, &sx, &sy);
            tilt_tick(&a, 1.0f / 60.0f, true, sx, sy, 50.0f, i / 60.0); /* 50 deg/s: "moving" */
            if (zero_at < 0.0f && a.tx == 0.0f && i > 10) {
                zero_at = (float)i / 60.0f;
            }
        }
        if (zero_at < 4.0f || zero_at > 4.6f || a.backstops != 1 || !a.backstop_logged || a.ceilings != 0) {
            return failf(t, "backstop: back to 0 at %.2f s (want 4.0..4.6), backstop %d, ceiling %d", (double)zero_at, a.backstops, a.ceilings);
        }
        mg_log_info("autotest tilt: backstop: a stuck target while the sensor reports turning is back to 0 at %.2f s, logged once; the ceiling did not fire", (double)zero_at);
    }
    /* 11 */
    const float off = tilt_strength(0, false, false), red = tilt_strength(2, true, false), low = tilt_strength(2, false, true);
    if (off != 0.0f || red != 0.0f || low > 0.5f) {
        return failf(t, "strength: Oprit %.2f, reduced %.2f, Low Power %.2f", (double)off, (double)red, (double)low);
    }
    mg_log_info("autotest tilt: still at 10 / 30 / 60 deg: exactly 0 from 2 s; ramp peak %.3f, rest %.0f ms after; flick in both rotations %.3f / %.3f; sine mean %.2f pt, rest %.0f ms after; noise %.3f pt; S Oprit %.1f, reduced %.1f, Low Power %.1f",
                (double)rp.peak, (double)(rp.rest_t * 1000.0f), (double)fl.peak, (double)fl2.peak, (double)sn.mean, (double)(sn.rest_t * 1000.0f),
                (double)(fabsf(nz.peak) * k_ui.tilt_depth[4]), (double)off, (double)red, (double)low);
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
