#include "autotest_haptics.h"

#include <math.h>

#include "autotest.h"
#include "core/log.h"
#include "frontend.h"
#include "ui/haptics.h"

#define FINGER 95

static int failf(HapticTest *t, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(t->why, sizeof(t->why), fmt, ap);
    va_end(ap);
    return -1;
}

static void escape(void)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = SDL_SCANCODE_ESCAPE;
    e.key.down = true;
    SDL_PushEvent(&e);
    e.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    SDL_PushEvent(&e);
}

static void next(HapticTest *t, double now, int stage)
{
    t->stage = stage, t->sub = 0, t->item = 0, t->at = now;
}

static uint64_t total(void)
{
    const HapticRecord *r;
    uint64_t n;
    ui_haptic_records(&r, &n);
    return n;
}

/* the i-th record since `mark` (NULL past the end) */
static const HapticRecord *since(uint64_t mark, int i)
{
    const HapticRecord *r;
    uint64_t n;
    ui_haptic_records(&r, &n);
    return mark + (uint64_t)i < n ? &r[(mark + (uint64_t)i) % HAPTIC_RECORDS] : NULL;
}

static int count_since(uint64_t mark) { return (int)(total() - mark); }

static bool has(uint64_t mark, HapticId id, uint64_t frame)
{
    for (int i = 0; since(mark, i); i++) {
        if (since(mark, i)->id == id && (frame == 0 || since(mark, i)->frame == frame)) {
            return true;
        }
    }
    return false;
}

/* 2: the table's own order */
static int check_table(HapticTest *t)
{
    for (int i = 0; i < HAPTIC_COUNT; i++) {
        if (k_haptics[i].n < 1 || !k_haptics[i].name) {
            return failf(t, "table entry %d has no events", i);
        }
        if (k_haptics[i].rank == 5 && i != HAPTIC_CONFIRM && i != HAPTIC_COMPLETE && i != HAPTIC_DEMO_R5) {
            return failf(t, "%s is R5: only destructive confirms and celebrations may be", k_haptics[i].name);
        }
    }
    const float link = k_haptics[HAPTIC_LINK].ev[0].intensity, sec = k_haptics[HAPTIC_SECONDARY].ev[0].intensity;
    const float pri = k_haptics[HAPTIC_PRIMARY].ev[0].intensity, main_ = k_haptics[HAPTIC_MAIN].ev[0].intensity;
    if (!(link < sec && sec < pri && pri < main_)) {
        return failf(t, "release order broken: link %.2f secondary %.2f primary %.2f main %.2f", (double)link, (double)sec, (double)pri, (double)main_);
    }
    for (int c = 0; c < HC_COUNT; c++) {
        const HapticId d = k_haptic_class[c].down, u = k_haptic_class[c].up;
        if (d != HAPTIC_NONE && (u == HAPTIC_NONE ? false : k_haptics[d].ev[0].intensity >= k_haptics[u].ev[0].intensity)) {
            return failf(t, "class %d: touch down %.2f is not below its release %.2f", c, (double)k_haptics[d].ev[0].intensity, (double)k_haptics[u].ev[0].intensity);
        }
    }
    mg_log_info("autotest haptics: table: %d entries, %d widget kinds; release link %.2f < secondary %.2f < primary %.2f < main %.2f; downs below releases; R5 only confirm / celebrate",
                HAPTIC_COUNT, HC_COUNT - 1, (double)link, (double)sec, (double)pri, (double)main_);
    return 0;
}

enum { S_START, S_SCREEN, S_REWARD, S_WRONG, S_LIMIT, S_LEVEL, S_ROW, S_LIFE, S_WALK, S_DONE };

/* one test-screen press: down, 0.12 s (past the 50 ms window), up; then what it recorded */
static int press(HapticTest *t, MgApp *app, double now, float x, float y, bool drag_off)
{
    if (t->sub == 0) {
        t->mark = total();
        autotest_finger(app, SDL_EVENT_FINGER_DOWN, FINGER, x, y);
        t->sub = 1, t->at = now;
    } else if (t->sub == 1 && now - t->at > 0.12) {
        if (drag_off) {
            autotest_finger(app, SDL_EVENT_FINGER_MOTION, FINGER, x, y + 400.0f);
        }
        autotest_finger(app, SDL_EVENT_FINGER_UP, FINGER, drag_off ? x : x, drag_off ? y + 400.0f : y);
        t->sub = 2, t->at = now;
    } else if (t->sub == 2 && now - t->at > 0.15) {
        t->sub = 0;
        return 1;
    }
    return 0;
}

int ht_run(HapticTest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double now)
{
    UiTest *ut = p->uitest;
    switch (t->stage) {
    case S_START:
        if (t->at == 0.0) {
            t->at = now;
            ui_haptic_force_supported(1); /* the recorder stands in for a Taptic Engine */
            out->set_haptics = 1 + HAPTIC_LEVEL_FULL;
        }
        if (now - t->at < 2.0 || !p->fe || p->fe->state != FE_MAIN_MENU) {
            return now - t->at > 30.0 ? failf(t, "never reached the menu") : 0;
        }
        if (check_table(t) < 0) {
            return -1;
        }
        ut->screen = true;
        next(t, now, S_SCREEN);
        return 0;
    case S_SCREEN: { /* 1 + 3: the test screen's widgets, each on its frames */
        static const struct { int b; HapticId down, up; } k[] = {
            {0, HAPTIC_PRIMARY_DOWN, HAPTIC_PRIMARY}, {1, HAPTIC_SECONDARY_DOWN, HAPTIC_SECONDARY}, {2, HAPTIC_SECONDARY_DOWN, HAPTIC_SECONDARY},
            {3, HAPTIC_DESTRUCTIVE_DOWN, HAPTIC_DESTRUCTIVE}, {4, HAPTIC_NONE, HAPTIC_NONE}};
        if (now - t->at < 1.5 && t->item == 0 && t->sub == 0) {
            return 0;
        }
        if (t->item < 5) {
            const int b = k[t->item].b;
            if (!press(t, app, now, ut->bx[b], ut->by[b], false)) {
                return 0;
            }
            if (k[t->item].down == HAPTIC_NONE) { /* disabled: nothing */
                if (count_since(t->mark)) {
                    return failf(t, "the disabled button recorded %d haptics", count_since(t->mark));
                }
            } else {
                if (!has(t->mark, k[t->item].down, ut->down_fi[b])) {
                    return failf(t, "button %d: no %s on its touch-down frame %llu", b, k_haptics[k[t->item].down].name, (unsigned long long)ut->down_fi[b]);
                }
                if (!has(t->mark, k[t->item].up, ut->up_fi[b])) {
                    return failf(t, "button %d: no %s on its action frame %llu", b, k_haptics[k[t->item].up].name, (unsigned long long)ut->up_fi[b]);
                }
            }
            t->item++;
            return 0;
        }
        /* the toggle (on), a tab, then nothing on the counter */
        float x, y;
        const char *ids[2] = {"ut_toggle", "ut_tabs2"};
        const HapticId want[2] = {ut->toggle ? HAPTIC_TOGGLE_OFF : HAPTIC_TOGGLE_ON, ut->tab == 2 ? HAPTIC_NONE : HAPTIC_TAB};
        const int i = t->item - 5;
        if (i < 2) {
            if (!ui_find(p->ui, ids[i], &x, &y)) {
                return failf(t, "%s not on the test screen", ids[i]);
            }
            if (t->sub == 0) {
                t->expect = want[i];
            }
            if (!press(t, app, now, x, y, false)) {
                return 0;
            }
            if (t->expect != HAPTIC_NONE && !has(t->mark, (HapticId)t->expect, 0)) {
                return failf(t, "%s recorded no %s", ids[i], k_haptics[t->expect].name);
            }
            t->item++;
            return 0;
        }
        mg_log_info("autotest haptics: test screen: primary, secondary, icon and destructive record their down and action entries on their input frames; disabled records nothing; toggle and tab record theirs");
        next(t, now, S_REWARD);
        return 0;
    }
    case S_REWARD: /* 6: eight rising taps ending at R4 */
        if (t->sub == 0 && now - t->at > 0.5) {
            t->mark = total();
            ut->cmd_reward = 40;
            t->sub = 1, t->at = now;
        } else if (t->sub == 1 && now - t->at > 2.5) {
            float last = 0.0f;
            int n = 0;
            for (int i = 0; since(t->mark, i); i++) {
                const HapticRecord *r = since(t->mark, i);
                if (r->id < HAPTIC_REWARD_0 || r->id > HAPTIC_REWARD_7) {
                    continue;
                }
                if (r->id != HAPTIC_REWARD_0 + n || r->intensity <= last) {
                    return failf(t, "reward tap %d: %s at %.2f after %.2f", n, k_haptics[r->id].name, (double)r->intensity, (double)last);
                }
                last = r->intensity, n++;
            }
            if (n != 8 || fabsf(last - HR4) > 1e-4f) {
                return failf(t, "%d reward taps, the last at %.2f (want 8, ending at R4 %.2f)", n, (double)last, (double)HR4);
            }
            mg_log_info("autotest haptics: reward of 8 tokens: 8 taps rising to %.2f (R4)", (double)last);
            next(t, now, S_WRONG);
        }
        return 0;
    case S_WRONG: { /* 6: ERROR, no SUCCESS */
        float x, y;
        if (t->sub == 0) {
            ut->answer[0] = ut->answer[1] = 0;
            if (now - t->at < 0.3) {
                return 0;
            }
        }
        if (t->sub == 0) { /* its spot before the tap: a decided row is no longer pressable */
            if (!ui_find(p->ui, "ut_ans1", &x, &y)) {
                return failf(t, "no answer rows");
            }
            t->x = x, t->y = y;
        }
        if (!press(t, app, now, t->x, t->y, false)) {
            return 0;
        }
        if (!has(t->mark, HAPTIC_WRONG, 0) || has(t->mark, HAPTIC_CORRECT, 0)) {
            return failf(t, "a wrong answer: ERROR %d, SUCCESS %d", has(t->mark, HAPTIC_WRONG, 0), has(t->mark, HAPTIC_CORRECT, 0));
        }
        mg_log_info("autotest haptics: wrong answer: ERROR, no SUCCESS");
        next(t, now, S_LIMIT);
        return 0;
    }
    case S_LIMIT: { /* 4: through the API with a synthetic clock (frames cannot be 10 ms apart) */
        if (now - t->at < 0.5) {
            return 0;
        }
        uint64_t mark = total();
        const double base = now + 1.0; /* ahead of the real clock, which then waits for the window */
        for (int i = 0; i < 20; i++) { /* 20 presses in 200 ms: down + action each */
            ui_haptics_frame(app->frame_index, base + 0.01 * i);
            ui_haptic(HAPTIC_PRIMARY_DOWN);
            ui_haptics_flush();
            ui_haptics_frame(app->frame_index, base + 0.01 * i + 0.005);
            ui_haptic(HAPTIC_PRIMARY);
            ui_haptics_flush();
        }
        const int presses = count_since(mark);
        mark = total();
        const double b2 = base + 1.0;
        for (int f = 0; f <= 30; f++) { /* a slider end to end in 0.5 s: a step every frame, the end bump last */
            ui_haptics_frame(app->frame_index, b2 + f / 60.0);
            ui_haptic(f == 30 ? HAPTIC_SLIDER_END : HAPTIC_SLIDER_STEP);
            ui_haptics_flush();
        }
        int ticks = 0, ends = 0;
        for (int i = 0; since(mark, i); i++) {
            ticks += since(mark, i)->id == HAPTIC_SLIDER_STEP;
            ends += since(mark, i)->id == HAPTIC_SLIDER_END;
        }
        ui_haptics_frame(app->frame_index, now);
        if (presses > 4 || ticks > 10 || ends != 1) {
            return failf(t, "limiter: %d starts for 20 presses in 200 ms (max 4), %d slider ticks (max 10), %d end bumps (want 1)", presses, ticks, ends);
        }
        mg_log_info("autotest haptics: limiter: 20 presses in 200 ms -> %d starts; slider in 0.5 s -> %d ticks + %d end bump", presses, ticks, ends);
        next(t, now, S_LEVEL);
        return 0;
    }
    case S_LEVEL: /* 5: Oprit, Fin */
        if (t->item == 0) {
            out->set_haptics = 1 + HAPTIC_LEVEL_OFF;
            t->item = 1, t->at = now;
            return 0;
        }
        if (now - t->at < 2.2) { /* the real clock catches up with the limiter's synthetic one */
            return 0;
        }
        if (t->item == 1) {
            if (!press(t, app, now, ut->bx[0], ut->by[0], false)) {
                return 0;
            }
            if (count_since(t->mark)) {
                return failf(t, "Oprit recorded %d haptics", count_since(t->mark));
            }
            out->set_haptics = 1 + HAPTIC_LEVEL_FINE;
            t->item = 2;
            return 0;
        }
        if (t->item == 2) {
            if (!press(t, app, now, ut->bx[0], ut->by[0], false)) {
                return 0;
            }
            const HapticRecord *d = since(t->mark, 0), *u = since(t->mark, 1);
            if (!d || !u || fabsf(d->intensity - 0.6f * HR2) > 1e-3f || fabsf(u->intensity - 0.6f * HR3) > 1e-3f) {
                return failf(t, "Fin: primary down %.3f / action %.3f, want %.3f / %.3f", d ? (double)d->intensity : -1.0, u ? (double)u->intensity : -1.0,
                             (double)(0.6f * HR2), (double)(0.6f * HR3));
            }
            t->item = 3;
            return 0;
        }
        if (t->item == 3) { /* an R1 entry under Fin: nothing */
            float x, y;
            if (!ui_find(p->ui, ut->tab == 0 ? "ut_tabs1" : "ut_tabs0", &x, &y)) {
                return failf(t, "no tab to press");
            }
            if (!press(t, app, now, x, y, false)) {
                return 0;
            }
            if (count_since(t->mark)) {
                return failf(t, "Fin recorded an R1 entry (%s)", k_haptics[since(t->mark, 0)->id].name);
            }
            out->set_haptics = 1 + HAPTIC_LEVEL_FULL;
            mg_log_info("autotest haptics: Oprit records nothing; Fin records 0.6x (%.2f / %.2f) and drops R1", (double)(0.6f * HR2), (double)(0.6f * HR3));
            next(t, now, S_ROW);
        }
        return 0;
    case S_ROW: { /* 5: the Vibrații row only with the capability */
        float x, y;
        if (t->sub == 0) {
            ut->screen = false;
            frontend_go(p->fe, FE_SETTINGS);
            t->sub = 1, t->at = now;
        } else if (t->sub == 1 && now - t->at > 1.5) {
            if (!ui_find(p->ui, "vib0", &x, &y)) {
                return failf(t, "the Vibrații row is missing with the capability on");
            }
            ui_haptic_force_supported(0);
            t->sub = 2, t->at = now;
        } else if (t->sub == 2 && now - t->at > 0.5) {
            if (ui_find(p->ui, "vib0", &x, &y)) {
                return failf(t, "the Vibrații row shows without haptic hardware");
            }
            ui_haptic_force_supported(1);
            escape();
            mg_log_info("autotest haptics: the Vibrații row shows with the capability and is absent without it");
            next(t, now, S_LIFE);
        }
        return 0;
    }
    case S_LIFE: /* 7 */
        if (t->sub == 0 && t->item == 0) {
            if (now - t->at < 1.0) {
                return 0;
            }
            ut->screen = true;
            ui_haptics_active(false); /* background */
            ui_haptics_active(true);  /* foreground */
            t->item = 1, t->at = now;
            return 0;
        }
        if (t->item == 1) {
            if (now - t->at < 1.0 && t->sub == 0) {
                return 0;
            }
            if (!press(t, app, now, ut->bx[0], ut->by[0], false)) {
                return 0;
            }
            if (!has(t->mark, HAPTIC_PRIMARY, 0)) {
                return failf(t, "after background and foreground a press recorded nothing");
            }
            const int before = ui_haptic_rebuilds();
            ui_haptic_simulate_reset();
            if (ui_haptic_rebuilds() != before + 1) {
                return failf(t, "a simulated engine reset did not rebuild the players (%d -> %d)", before, ui_haptic_rebuilds());
            }
            mg_log_info("autotest haptics: background -> foreground: presses still record; an engine reset rebuilds the players");
            ut->screen = false;
            t->screen_i = 0;
            next(t, now, S_WALK);
        }
        return 0;
    case S_WALK: { /* 1: every pressable with a touch-down entry on every screen records it */
        static const struct { const char *tap; int state; const char *name; } screens[] = {
            {NULL, FE_MAIN_MENU, "menu"}, {"menurow31", FE_CREATOR, "Locker"}, {"menurow2", FE_SETTINGS, "Settings"},
            {"account_chip", FE_FRIENDS, "Friends"}, {"menurow4", FE_MAP, "map"}};
        const int n = (int)(sizeof(screens) / sizeof(screens[0]));
        if (t->screen_i >= n) {
            next(t, now, S_DONE);
            return 0;
        }
        if (t->sub == 0) { /* back to the menu, open the screen */
            if (p->fe->state != FE_MAIN_MENU) {
                if (now - t->at > 0.6) {
                    escape();
                    t->at = now;
                }
                return 0;
            }
            if (now - t->at < 0.8) {
                return 0;
            }
            float x, y;
            if (screens[t->screen_i].tap) {
                if (!ui_find(p->ui, screens[t->screen_i].tap, &x, &y)) {
                    return failf(t, "%s: %s not on the menu", screens[t->screen_i].name, screens[t->screen_i].tap);
                }
                autotest_finger(app, SDL_EVENT_FINGER_DOWN, FINGER, x, y);
                t->x = x, t->y = y;
                t->sub = 1;
            } else {
                t->sub = 2;
            }
            t->at = now;
            return 0;
        }
        if (t->sub == 1 && now - t->at > 0.08) {
            autotest_finger(app, SDL_EVENT_FINGER_UP, FINGER, t->x, t->y);
            t->sub = 2, t->at = now;
            return 0;
        }
        if (t->sub == 2) {
            if ((int)p->fe->state != screens[t->screen_i].state) {
                if (now - t->at > 2.0 && screens[t->screen_i].state == FE_FRIENDS) {
                    frontend_go(p->fe, FE_FRIENDS);
                }
                return now - t->at > 4.0 ? failf(t, "%s never opened", screens[t->screen_i].name) : 0;
            }
            if (p->move_state != 0) { /* LIB-1: the map's camera move takes no input; 2 s from its end */
                t->at = now;
                return 0;
            }
            if (now - t->at > 2.0) {
                t->item = 0, t->pressed = t->recorded = t->skipped = 0;
                t->sub = 3;
            }
            return 0;
        }
        /* sub 3..: walk the widgets: down, check the down entry, drag off and up (nothing fires) */
        const Ui *ui = p->ui;
        if (t->sub == 3) {
            while (t->item < ui->seen_count) {
                const int hc = ui->seen[t->item].hclass;
                const float x = ui->seen[t->item].x + ui->seen[t->item].w * 0.5f, y = ui->seen[t->item].y + ui->seen[t->item].h * 0.5f;
                const bool on = x > 0.0f && x < app->width_pt && y > 0.0f && y < app->height_pt;
                if (!ui->seen[t->item].rect_only && on && hc >= 0 && hc < HC_COUNT && k_haptic_class[hc].down != HAPTIC_NONE) {
                    break;
                }
                t->skipped += !ui->seen[t->item].rect_only && on;
                t->item++;
            }
            if (t->item >= ui->seen_count) {
                mg_log_info("autotest haptics: %s: %d pressables with a touch-down entry, %d recorded it; %d others have none (rows, tabs, toggles: release only)",
                            screens[t->screen_i].name, t->pressed, t->recorded, t->skipped);
                if (t->pressed != t->recorded) {
                    return failf(t, "%s: %d of %d presses recorded nothing", screens[t->screen_i].name, t->pressed - t->recorded, t->pressed);
                }
                t->screen_i++;
                t->sub = 0, t->at = now;
                escape();
                return 0;
            }
            t->x = ui->seen[t->item].x + ui->seen[t->item].w * 0.5f, t->y = ui->seen[t->item].y + ui->seen[t->item].h * 0.5f;
            t->expect = k_haptic_class[ui->seen[t->item].hclass].down;
            t->mark = total();
            autotest_finger(app, SDL_EVENT_FINGER_DOWN, FINGER, t->x, t->y);
            t->sub = 4, t->at = now;
            return 0;
        }
        if (t->sub == 4 && now - t->at > 0.1) {
            t->pressed++;
            t->recorded += has(t->mark, (HapticId)t->expect, 0);
            if (!has(t->mark, (HapticId)t->expect, 0)) {
                mg_log_error("autotest haptics: %s: widget %d at (%.0f, %.0f) recorded no %s", screens[t->screen_i].name, t->item, (double)t->x, (double)t->y,
                             k_haptics[t->expect].name);
            }
            const float off = t->y < app->height_pt * 0.5f ? app->height_pt - 4.0f : 4.0f; /* drag off: release far away */
            autotest_finger(app, SDL_EVENT_FINGER_MOTION, FINGER, app->width_pt * 0.5f, off);
            autotest_finger(app, SDL_EVENT_FINGER_UP, FINGER, app->width_pt * 0.5f, off);
            t->sub = 5, t->at = now;
            return 0;
        }
        if (t->sub == 5 && now - t->at > 0.12) {
            t->item++;
            t->sub = 3;
        }
        return 0;
    }
    default:
        ui_haptic_force_supported(-1);
        mg_log_info("autotest haptics: done");
        return 1;
    }
}
