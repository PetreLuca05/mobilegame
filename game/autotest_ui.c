#include "autotest_ui.h"

#include <math.h>
#include <string.h>

#include "autotest.h"
#include "core/log.h"
#include "frontend.h"
#include "strings.h"
#include "ui/ui_anim.h"
#include "ui/ui_style.h"
#include "ui/text_check.h"
#include "ui/ui_widgets.h"

#define UT_FINGER 91
#define FRAME_S (1.0 / 60.0)

static const char *const k_button_id[UT_BUTTONS] = {"ut_b0", "ut_b1", "ut_b2", "ut_b3", "ut_b4"};

/* TILT-1: the lip (a shape carrying tilt[1]) and the face drawn right after it, from instances n0..n1 */
static void capture(UiTest *t, int r, GfxContext *g, uint32_t n0)
{
    uint32_t n1;
    const GfxUiInst *in = gfx_ui_instances(g, &n1);
    t->tref[r].ok = false;
    for (uint32_t i = n0; i + 1 < n1; i++) {
        if (in[i].kind == (float)GFX_UI_SHAPE && in[i].tilt[1] > 0.0f && in[i + 1].kind == (float)GFX_UI_SHAPE) {
            memcpy(t->tref[r].lip, in[i].rect, sizeof(t->tref[r].lip));
            memcpy(t->tref[r].face, in[i + 1].rect, sizeof(t->tref[r].face));
            t->tref[r].lip_depth = in[i].tilt[0], t->tref[r].face_depth = in[i + 1].tilt[0], t->tref[r].lip_h = in[i].tilt[1];
            t->tref[r].ok = true;
            return;
        }
    }
}

/* ------------------------------------------------------------------ the test screen */

void ui_test_draw(UiTest *t, Ui *ui, MgApp *app)
{
    if (!t->screen) {
        return;
    }
    text_check_exempt_push(); /* the test's own widget sheet (Primary, Unu ...): only --autotest ui draws it */
    t->frame++;
    t->seen_from = ui->seen_count;
    for (int i = 0; i < MG_MAX_TOUCHES; i++) {
        if (ui->in->touches[i].ended) {
            t->up_frame = t->frame;
        }
    }
    const float l = mg_maxf(app->safe_left, 0.0f) + 28.0f, r = app->width_pt - mg_maxf(app->safe_right, 0.0f) - 28.0f;
    const float top = mg_maxf(app->safe_top, 8.0f) + 20.0f, bottom = app->height_pt - mg_maxf(app->safe_bottom, 8.0f) - 8.0f;
    uint32_t n0;
    gfx_ui_instances(ui->gfx, &n0);
    ui_card(ui, l - 16.0f, top - 12.0f, r - l + 32.0f, bottom - top + 12.0f);
    capture(t, 0, ui->gfx, n0);
    const double now = ui->anim ? ui->anim->now : 0.0;
    const float gap = 12.0f, size = k_ui.icon_button, bw = (r - l - 4.0f * gap - size) / 4.0f;
    float x = l;
    for (int b = 0; b < UT_BUTTONS; b++) {
        static const char *const names[UT_BUTTONS] = {"Primary", "Secondary", "", "Destructive", "Disabled"};
        static const UiVariant v[UT_BUTTONS] = {UI_V_PRIMARY, UI_V_SECONDARY, UI_V_ACCENT, UI_V_DESTRUCTIVE, UI_V_DISABLED};
        char label[48];
        SDL_snprintf(label, sizeof(label), "%s##%s", names[b], k_button_id[b]);
        gfx_ui_instances(ui->gfx, &n0);
        const bool hit = b == 2 ? ui_icon_button(ui, k_button_id[b], UI_ICON_PLUS, x, top, size, v[b])
                                : ui_button_v(ui, label, x, top, bw, k_ui.button_h, v[b], UI_F_SMALL);
        if (b == 0) {
            capture(t, 1, ui->gfx, n0);
        }
        t->bx[b] = x + (b == 2 ? size : bw) * 0.5f, t->by[b] = top + (b == 2 ? size : k_ui.button_h) * 0.5f;
        const bool active = ui->active_id == ui_key(k_button_id[b]) && ui->press_inside;
        if (active && !t->was_active[b]) {
            t->t_down[b] = now;
            t->down_fi[b] = app->frame_index;
        }
        t->was_active[b] = active;
        if (hit) {
            t->fired[b]++;
            t->fired_frame[b] = t->frame;
            t->t_up[b] = now;
            t->up_fi[b] = app->frame_index;
        }
        x += (b == 2 ? size : bw) + gap;
    }
    const float y2 = top + k_ui.button_h + 22.0f;
    ui_switch(ui, "ut_toggle", l, y2, &t->toggle);
    static const char *const tabs[3] = {"Unu", "Doi", "Trei"};
    ui_tabs(ui, "ut_tabs", tabs, 3, &t->tab, l + k_ui.toggle_w + 24.0f, y2, 300.0f, 44.0f);
    gfx_ui_instances(ui->gfx, &n0);
    ui_counter(ui, "ut_counter", l + k_ui.toggle_w + 348.0f, y2, 44.0f, "", t->counter, "", &t->counter_shown);
    capture(t, 2, ui->gfx, n0);
    const float y3 = y2 + 44.0f + 24.0f, aw = (r - l - gap) * 0.5f;
    if (ui_answer(ui, "ut_ans0", "Corect", l, y3, aw, (UiAnswer)t->answer[0])) {
        t->answer[0] = UI_ANS_OK, t->answer[1] = UI_ANS_DIM;
        ui_haptic(HAPTIC_CORRECT);
        ui_pop(ui, "ut_ans0");
        ui_tier_at(ui, "ut_ans0", UI_T2);
    }
    if (ui_answer(ui, "ut_ans1", "Greșit", l + aw + gap, y3, aw, (UiAnswer)t->answer[1]) || t->cmd_wrong) {
        t->cmd_wrong = false;
        t->answer[1] = UI_ANS_BAD, t->answer[0] = UI_ANS_REVEAL;
        ui_haptic(HAPTIC_WRONG);
        ui_shake(ui, "ut_ans1", 10.0f);
        ui_tier_at(ui, "ut_ans1", UI_T2_FAIL);
    }
    float cx, cy, cw, ch;
    if (t->cmd_reward > 0 && ui->fx && ui_rect_of(ui, "ut_counter", &cx, &cy, &cw, &ch)) {
        t->counter += t->cmd_reward;
        ui_fx_reward(ui->fx, app->width_pt * 0.5f, y3 + 30.0f, ui_key("ut_counter"), cx + cw * 0.5f, cy + ch * 0.5f, t->cmd_reward);
        t->cmd_reward = 0;
    }
    if (t->cmd_tier > 0 && ui->fx) {
        const UiTier tier = (UiTier)(t->cmd_tier - 1);
        if (tier == UI_T3) {
            ui_fx_banner(ui->fx, str("wm_completed"));
        }
        ui_fx_tier(ui->fx, tier, app->width_pt * 0.5f, app->height_pt * 0.5f, true);
        t->cmd_tier = 0;
    }
    text_check_exempt_pop();
}

/* ------------------------------------------------------------------ the script */

static int failf(UiTest *t, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(t->why, sizeof(t->why), fmt, ap);
    va_end(ap);
    return -1;
}

static void key(MgApp *app, SDL_Scancode sc)
{
    (void)app;
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = sc;
    e.key.down = true;
    SDL_PushEvent(&e);
    e.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    SDL_PushEvent(&e);
}

static bool centre(const AutotestProbe *p, const char *id, float *x, float *y) { return ui_find(p->ui, id, x, y); }

static void next(UiTest *t, double now, int stage)
{
    t->stage = stage, t->sub = 0, t->at = now;
}

static float get(const AutotestProbe *p, const char *id, UiProp prop, float rest) { return ui_anim_get(p->ui->anim, ui_key(id), prop, rest); }

/* the target and safe-rect check over last frame's tappable widgets */
static int check_targets(UiTest *t, MgApp *app, const AutotestProbe *p, const char *screen, bool safe, int from)
{
    const Ui *ui = p->ui;
    const float sl = app->safe_left, sr = app->width_pt - app->safe_right, st = app->safe_top, sb = app->height_pt - app->safe_bottom;
    int checked = 0;
    for (int i = from; i < ui->seen_count; i++) {
        const float x = ui->seen[i].x, y = ui->seen[i].y, w = ui->seen[i].w, h = ui->seen[i].h;
        if (ui->seen[i].rect_only || y + h <= 0.0f || y >= app->height_pt || x + w <= 0.0f || x >= app->width_pt) {
            continue; /* not a target, or scrolled away */
        }
        checked++;
        if (!ui->seen[i].partial && (w < k_ui.target_min - 0.5f || h < k_ui.target_min - 0.5f)) { /* a scrolled-in part: full size */
            return failf(t, "%s: a widget at (%.0f, %.0f) is %.0f x %.0f pt, under %.0f", screen, x, y, w, h, k_ui.target_min);
        }
        const bool on_screen_v = y >= 0.0f && y + h <= app->height_pt;
        if (safe && (x < sl - 0.5f || x + w > sr + 0.5f || (on_screen_v && (y < st - 0.5f || y + h > sb + 0.5f)))) {
            return failf(t, "%s: a widget at (%.0f, %.0f, %.0f x %.0f) is outside the safe rect", screen, x, y, w, h);
        }
    }
    mg_log_info("autotest ui: %s: %d tappable widgets, all >= %.0f pt%s", screen, checked, k_ui.target_min, safe ? ", inside the safe rect" : "");
    return checked > 0 ? 0 : failf(t, "%s: no tappable widget seen", screen);
}

enum { S_START, S_PRESS, S_TEN, S_DRAG, S_TOGGLE, S_TAB, S_REWARD, S_WRONG, S_SHOTS, S_SCREENS, S_DONE };

int ut_run(UiTest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double now)
{
    const Ui *ui = p->ui;
    UiFx *fx = ui->fx;
    if (!ui->anim || !fx) {
        return failf(t, "the UI has no motion or effects attached");
    }
    const double anow = ui->anim->now;
    const bool red = t->motion_pass == 1;
    /* 10: no frame with more than one pass (no blur); the frames of a picture are exempt */
    {
        GfxStats gs;
        gfx_get_stats(app->gfx, &gs);
        if (t->skip > 0) {
            t->skip--;
        } else if ((int)(gs.passes - gs.head_passes) > t->max_passes) { /* a new head's atlas pass is not a frame pass */
            t->max_passes = (int)(gs.passes - gs.head_passes);
            mg_log_info("autotest ui: %d passes at stage %d.%d (screen %d)", t->max_passes, t->stage, t->sub, t->screen_i);
        }
    }
    /* 7: reduced motion leaves no particle, ring, token or shake behind */
    if (red && t->fx_clean) {
        int np, nr, nt;
        ui_fx_live(fx, &np, &nr, &nt);
        if (np || nr || nt || ui_fx_shake_x(fx) != 0.0f || get(p, "ut_ans1", UI_X, 0.0f) != 0.0f) {
            return failf(t, "reduced motion: %d particles, %d rings, %d tokens, shake %.2f at stage %d", np, nr, nt, (double)ui_fx_shake_x(fx), t->stage);
        }
    }
    switch (t->stage) {
    case S_START:
        if (t->sub == 0) {
            t->screen = true;
            t->sub = 1;
            if (p->fe && p->fe->state != FE_MAIN_MENU) {
                return 0;
            }
        }
        if (now - t->at > 1.5 && centre(p, "ut_b0", &t->cx, &t->cy)) {
            t->fx_clean = red;
            t->button = 0;
            next(t, now, S_PRESS);
        }
        return 0;
    case S_PRESS: { /* 1: press 90 ms, release 480 ms, the action on the touch-up frame */
        const char *id = k_button_id[t->button];
        if (t->sub == 0) {
            if (t->bx[t->button] <= 0.0f) {
                return failf(t, "button %s not on screen", id);
            }
            t->cx = t->bx[t->button], t->cy = t->by[t->button];
            t->taps = t->fired[t->button];
            t->t_down[t->button] = 0.0;
            autotest_finger(app, SDL_EVENT_FINGER_DOWN, UT_FINGER, t->cx, t->cy);
            t->sub = 1, t->at = now;
        } else if (t->sub == 1) {
            if (t->button == 4) { /* disabled: neither moves nor fires */
                if (now - t->at < 0.3) {
                    return 0;
                }
                if (get(p, id, UI_SX, 1.0f) != 1.0f || get(p, id, UI_SY, 1.0f) != 1.0f || t->t_down[4] != 0.0) {
                    return failf(t, "the disabled button moved");
                }
                autotest_finger(app, SDL_EVENT_FINGER_UP, UT_FINGER, t->cx, t->cy);
                t->sub = 3, t->at = now;
                return 0;
            }
            if (t->t_down[t->button] == 0.0) {
                return now - t->at > 1.0 ? failf(t, "%s: the press never registered", id) : 0;
            }
            const double age = anow - t->t_down[t->button];
            if (age < k_ui.press_s) {
                return 0;
            }
            const float sx = get(p, id, UI_SX, 1.0f), sy = get(p, id, UI_SY, 1.0f);
            const float wx = red ? k_ui.reduced_press : k_ui.press_sx, wy = red ? k_ui.reduced_press : k_ui.press_sy;
            if (age > k_ui.press_s + 3.0 * FRAME_S || fabsf(sx - wx) > 0.01f || fabsf(sy - wy) > 0.01f) {
                return failf(t, "%s at %.0f ms: scale (%.3f, %.3f), want (%.2f, %.2f) +- 0.01", id, age * 1000.0, (double)sx, (double)sy, (double)wx, (double)wy);
            }
            autotest_finger(app, SDL_EVENT_FINGER_UP, UT_FINGER, t->cx, t->cy);
            t->sub = 2, t->at = now;
        } else if (t->sub == 2) {
            if (t->fired[t->button] == t->taps) {
                return now - t->at > 1.0 ? failf(t, "%s: no action after the touch-up", id) : 0;
            }
            if (t->fired[t->button] != t->taps + 1 || t->fired_frame[t->button] != t->up_frame) {
                return failf(t, "%s: %d actions, fired on frame %llu, touch-up on frame %llu", id, t->fired[t->button] - t->taps,
                             (unsigned long long)t->fired_frame[t->button], (unsigned long long)t->up_frame);
            }
            t->sub = 4; /* checked once: later touches may end on later frames */
        } else if (t->sub == 4) {
            if (anow - t->t_up[t->button] < k_ui.release_s + FRAME_S) {
                return 0;
            }
            const float sx = get(p, id, UI_SX, 1.0f), sy = get(p, id, UI_SY, 1.0f);
            if (fabsf(sx - 1.0f) > 0.002f || fabsf(sy - 1.0f) > 0.002f) {
                return failf(t, "%s: scale (%.3f, %.3f) %.0f ms after the release", id, (double)sx, (double)sy, (anow - t->t_up[t->button]) * 1000.0);
            }
            t->sub = 3;
        } else if (now - t->at > 0.2) {
            mg_log_info("autotest ui: %s%s press / release / fire ok", id, red ? " (reduced)" : "");
            if (++t->button < UT_BUTTONS) {
                t->sub = 0;
            } else {
                next(t, now, S_TEN);
            }
        }
        return 0;
    }
    case S_TEN: /* 2: ten taps in one second */
        if (t->sub == 0) {
            centre(p, "ut_b0", &t->cx, &t->cy);
            t->taps = t->fired[0];
            t->sub = 1, t->at = now;
            t->tap_index = t->tap_missed = t->tap_missed_slow = 0;
            t->tap_check_frame = 0;
            t->tap_max_dt = t->tap_worst_dt = 0.0f;
        }
        t->tap_max_dt = mg_maxf(t->tap_max_dt, app->frame_dt);
        if (t->tap_check_frame && app->frame_index >= t->tap_check_frame) { /* this tap's action, at most 2 frames late */
            const int want = t->taps + t->tap_index - t->tap_missed - t->tap_missed_slow;
            if (t->fired[0] < want) {
                const bool slow = t->tap_max_dt > 2.0f / 60.0f + 0.002f; /* a frame over two 60 Hz frames */
                t->tap_worst_dt = mg_maxf(t->tap_worst_dt, t->tap_max_dt);
                if (slow) {
                    t->tap_missed_slow++;
                    mg_log_info("autotest ui: WARN perf symptom: tap %d missed; the slowest frame around it took %.1f ms", t->tap_index, (double)(t->tap_max_dt * 1000.0f));
                } else {
                    t->tap_missed++;
                    mg_log_error("autotest ui: tap %d missed with frames of at most %.1f ms", t->tap_index, (double)(t->tap_max_dt * 1000.0f));
                }
            }
            t->tap_check_frame = 0;
        }
        if (t->sub <= 20) {
            const int k = t->sub - 1; /* even: down, odd: up, 50 ms apart */
            if (now - t->at >= 0.05 * k) {
                autotest_finger(app, (k & 1) ? SDL_EVENT_FINGER_UP : SDL_EVENT_FINGER_DOWN, UT_FINGER, t->cx, t->cy);
                if (k & 1) { /* processed next frame; checked 2 frames after that */
                    t->tap_index++;
                    t->tap_check_frame = app->frame_index + 3;
                } else {
                    t->tap_max_dt = 0.0f;
                }
                t->sub++;
            }
            return 0;
        }
        if (now - t->at < 1.3 || t->tap_check_frame) {
            return 0;
        }
        const int actions = t->fired[0] - t->taps;
        if (t->tap_missed > 0 || actions + t->tap_missed_slow != 10) {
            return failf(t, "ten taps in 1 s gave %d actions (%d missed on normal frames, %d on slow frames up to %.1f ms)", actions, t->tap_missed, t->tap_missed_slow,
                         (double)(t->tap_worst_dt * 1000.0f));
        }
        mg_log_info("autotest ui: ten taps in 1 s, %d actions on time%s", actions, t->tap_missed_slow ? " (see the WARN perf symptom above)" : "");
        next(t, now, S_DRAG);
        return 0;
    case S_DRAG: /* 3: down, drag off, up: no action, the release plays */
        if (t->sub == 0) {
            centre(p, "ut_b1", &t->cx, &t->cy);
            t->taps = t->fired[1];
            autotest_finger(app, SDL_EVENT_FINGER_DOWN, UT_FINGER, t->cx, t->cy);
            t->sub = 1, t->at = now;
        } else if (t->sub == 1 && now - t->at > 0.15) {
            autotest_finger(app, SDL_EVENT_FINGER_MOTION, UT_FINGER, t->cx, t->cy + 150.0f);
            t->sub = 2, t->at = now;
            t->moved = false;
        } else if (t->sub == 2) {
            if (ui_anim_busy(ui->anim, ui_key("ut_b1"), UI_SY) && get(p, "ut_b1", UI_SY, 1.0f) > (red ? k_ui.reduced_press : k_ui.press_sy) + 0.005f) {
                t->moved = true; /* springing back while the finger is still down, off the button */
            }
            if (now - t->at > 0.2) {
                autotest_finger(app, SDL_EVENT_FINGER_UP, UT_FINGER, t->cx, t->cy + 150.0f);
                t->sub = 3, t->at = now;
            }
        } else if (t->sub == 3 && now - t->at > 0.3) {
            if (t->fired[1] != t->taps) {
                return failf(t, "a drag off the button still fired it");
            }
            if (!t->moved) {
                return failf(t, "no release motion after dragging off");
            }
            mg_log_info("autotest ui: drag off: no action, the release played");
            next(t, now, S_TOGGLE);
        }
        return 0;
    case S_TOGGLE:
    case S_TAB: { /* 4: the indicator ends exactly on its target, after overshooting it (full motion) */
        const bool tg = t->stage == S_TOGGLE;
        const char *id = tg ? "ut_toggle" : "ut_tabs";
        const float target = tg ? (t->toggle ? 0.0f : 1.0f) : (t->tab == 2 ? 0.0f : 2.0f);
        if (t->sub == 0) {
            if (!centre(p, tg ? "ut_toggle" : (target == 2.0f ? "ut_tabs2" : "ut_tabs0"), &t->cx, &t->cy)) {
                return failf(t, "%s not on screen", id);
            }
            autotest_finger(app, SDL_EVENT_FINGER_DOWN, UT_FINGER, t->cx, t->cy);
            t->sub = 1, t->at = now;
            t->max_v = target > 0.5f ? -1e9f : 1e9f;
            t->cx = target; /* remember */
        } else if (t->sub == 1 && now - t->at > 0.05) {
            float x, y;
            centre(p, tg ? "ut_toggle" : (t->cx == 2.0f ? "ut_tabs2" : "ut_tabs0"), &x, &y);
            autotest_finger(app, SDL_EVENT_FINGER_UP, UT_FINGER, x, y);
            t->sub = 2, t->at = now;
        } else if (t->sub == 2) {
            const float rest = tg ? (t->toggle ? 1.0f : 0.0f) : (float)t->tab; /* what the widget draws once the tween is over */
            const float v = get(p, id, UI_VALUE, rest), want = t->cx;
            t->max_v = want > 0.5f ? mg_maxf(t->max_v, v) : mg_minf(t->max_v, v);
            if (now - t->at < 0.8 || (ui_anim_busy(ui->anim, ui_key(id), UI_VALUE) && now - t->at < 2.0)) {
                return 0; /* a slow frame can leave the spring still moving: wait for it to rest */
            }
            if (fabsf(v - want) > 1e-5f) {
                return failf(t, "%s ends at %.4f, target %.0f", id, (double)v, (double)want);
            }
            const bool over = want > 0.5f ? t->max_v > want + 1e-3f : t->max_v < want - 1e-3f;
            if (!red && !over) {
                return failf(t, "%s never overshot %.0f (extreme %.3f)", id, (double)want, (double)t->max_v);
            }
            mg_log_info("autotest ui: %s ends exactly on %.0f (extreme %.3f%s)", id, (double)want, (double)t->max_v, red ? ", reduced: linear" : "");
            next(t, now, tg ? S_TAB : S_REWARD);
        }
        return 0;
    }
    case S_REWARD: /* 5: a flight of 40 */
        if (t->sub == 0) {
            if (now - t->at < 0.5) {
                return 0;
            }
            t->counter0 = t->counter, t->coins0 = fx->coin_count;
            t->cmd_reward = 40;
            t->sub = 1, t->at = now;
        } else if (now - t->at > 2.5) {
            int np, nr, nt;
            ui_fx_live(fx, &np, &nr, &nt);
            if (t->counter_shown != t->counter0 + 40 || ui_fx_shown(fx, ui_key("ut_counter"), t->counter) != t->counter0 + 40) {
                return failf(t, "the counter shows %d, want %d", t->counter_shown, t->counter0 + 40);
            }
            const int coins = fx->coin_count - t->coins0;
            if (!red) {
                if (coins != 8) {
                    return failf(t, "%d coin ticks, want 8", coins);
                }
                for (int i = 1; i < 8; i++) {
                    if (fx->coin_pitch[(t->coins0 + i) & 7] <= fx->coin_pitch[(t->coins0 + i - 1) & 7]) {
                        return failf(t, "coin tick %d is not higher than the one before", i);
                    }
                }
            }
            if (np || nr || nt) {
                return failf(t, "pools not empty after the flight: %d particles, %d rings, %d tokens", np, nr, nt);
            }
            mg_log_info("autotest ui: reward +40 lands exactly (%d coin ticks%s), pools empty", coins, red ? ", reduced: count-up" : ", rising");
            next(t, now, S_WRONG);
        }
        return 0;
    case S_WRONG: /* 6: shake, bad, no particles */
        if (t->sub == 0) {
            t->answer[0] = t->answer[1] = UI_ANS_DEFAULT;
            int np;
            ui_fx_live(fx, &np, NULL, NULL);
            if (np || now - t->at < 0.3) {
                return now - t->at > 3.0 ? failf(t, "particles never died before the wrong answer") : 0;
            }
            if (!centre(p, "ut_ans1", &t->cx, &t->cy)) {
                return failf(t, "the answer rows are not on screen");
            }
            autotest_finger(app, SDL_EVENT_FINGER_DOWN, UT_FINGER, t->cx, t->cy);
            t->sub = 1, t->at = now;
            t->saw_shake = false;
        } else if (t->sub == 1 && now - t->at > 0.05) {
            autotest_finger(app, SDL_EVENT_FINGER_UP, UT_FINGER, t->cx, t->cy);
            t->sub = 2, t->at = now;
        } else if (t->sub == 2) {
            int np;
            ui_fx_live(fx, &np, NULL, NULL);
            if (np) {
                return failf(t, "a wrong answer made %d particles", np);
            }
            t->saw_shake = t->saw_shake || fabsf(get(p, "ut_ans1", UI_X, 0.0f)) > 1.0f;
            if (now - t->at < 0.6) {
                return 0;
            }
            if (t->answer[1] != UI_ANS_BAD || t->answer[0] != UI_ANS_REVEAL) {
                return failf(t, "the wrong answer is not bad, or the right one not revealed");
            }
            if (!red && !t->saw_shake) {
                return failf(t, "the wrong answer did not shake");
            }
            mg_log_info("autotest ui: wrong answer: bad, %s, 0 particles", red ? "no shake (reduced)" : "shook");
            if (t->motion_pass == 0) { /* 7: everything again with ui_motion = 1 */
                t->motion_pass = 1;
                t->reduced = true;
                next(t, now, S_START);
                t->sub = 1;
            } else {
                t->reduced = false;
                t->fx_clean = false;
                next(t, now, S_SHOTS);
            }
        }
        return 0;
    case S_SHOTS: { /* 9: the test screen at rest, then one frame mid-burst per tier */
        static const struct { int tier; const char *name; double wait; } shots[] = {
            {-1, "ui_test", 1.5}, {UI_T1, "ui_tier_t1", 0.12}, {UI_T1_TOGGLE, "ui_tier_t1_toggle", 0.12}, {UI_T2, "ui_tier_t2", 0.15},
            {UI_T2_FAIL, "ui_tier_t2_fail", 0.1}, {UI_T3, "ui_tier_t3", 0.45}, {UI_TN, "ui_tier_n", 0.3}};
        const int i = t->sub / 2;
        if (i >= (int)MG_ARRAY_COUNT(shots)) {
            t->screen = false;
            t->screen_i = 0;
            next(t, now, S_SCREENS);
            return 0;
        }
        if ((t->sub & 1) == 0) {
            if (now - t->at < 1.2) { /* the last one's effects die down */
                return 0;
            }
            if (shots[i].tier == UI_T2_FAIL) {
                t->cmd_wrong = true; /* what a tap on the wrong row does */
            } else if (shots[i].tier == UI_TN) {
                frontend_toast(p->fe, str("link_ok"), app->time);
                t->cmd_tier = 1 + UI_TN;
            } else if (shots[i].tier >= 0) {
                t->cmd_tier = 1 + shots[i].tier;
            }
            t->sub++, t->at = now;
        } else if (now - t->at >= shots[i].wait) {
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "%s", shots[i].name);
            t->skip = 4; /* the picture's frames render twice */
            if (i == 0 && check_targets(t, app, p, "test screen", true, t->seen_from) < 0) {
                return -1;
            }
            t->sub++, t->at = now;
        }
        return 0;
    }
    case S_SCREENS: { /* 8 + 9: every real screen at rest: targets, the safe rect, a picture */
        static const struct { const char *tap; int state; const char *name; bool safe; } screens[] = {
            {NULL, FE_MAIN_MENU, "ui_menu", false},  {"menurow31", FE_CREATOR, "ui_locker", false}, {"menurow2", FE_SETTINGS, "ui_settings", true},
            {"account_chip", FE_FRIENDS, "ui_friends", true}, {"menurow4", FE_MAP, "ui_map", true}};
        const int i = t->screen_i;
        if (i >= (int)MG_ARRAY_COUNT(screens)) {
            next(t, now, S_DONE);
            return 0;
        }
        if (t->sub == 0) { /* back on the menu, then open the screen */
            if (p->fe->state != FE_MAIN_MENU) {
                if (now - t->at > 0.5) {
                    key(app, SDL_SCANCODE_ESCAPE);
                    t->at = now;
                }
                return 0;
            }
            if (now - t->at < 0.8) {
                return 0;
            }
            float x, y;
            if (screens[i].tap) {
                if (!centre(p, screens[i].tap, &x, &y)) {
                    return failf(t, "%s: %s not on the menu", screens[i].name, screens[i].tap);
                }
                autotest_finger(app, SDL_EVENT_FINGER_DOWN, UT_FINGER, x, y);
                t->cx = x, t->cy = y;
                t->sub = 2, t->at = now; /* the up a few frames later */
                return 0;
            }
            t->sub = 1, t->at = now;
        } else if (t->sub == 2) {
            if (now - t->at > 0.06) {
                autotest_finger(app, SDL_EVENT_FINGER_UP, UT_FINGER, t->cx, t->cy);
                t->sub = 1, t->at = now;
            }
        } else if (t->sub == 1) {
            if ((int)p->fe->state != screens[i].state) {
                if (now - t->at > 2.0 && screens[i].state == FE_FRIENDS) { /* offline: the chip opens nothing */
                    frontend_go(p->fe, FE_FRIENDS);
                    t->at = now;
                }
                return now - t->at > 4.0 ? failf(t, "%s never opened", screens[i].name) : 0;
            }
            if (now - t->at < 2.0) { /* entered and settled (the map drives its camera in) */
                return 0;
            }
            if (check_targets(t, app, p, screens[i].name, screens[i].safe, 0) < 0) {
                return -1;
            }
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "%s", screens[i].name);
            t->skip = 4;
            t->screen_i++;
            t->sub = 0, t->at = now;
        }
        return 0;
    }
    default:
        if (t->max_passes > 1) {
            return failf(t, "a frame had %d passes (a blur or a second UI pass)", t->max_passes);
        }
        mg_log_info("autotest ui: at most %d pass per frame, no blur", t->max_passes);
        return 1;
    }
}
