#include "autotest_stick.h"

#include <math.h>

#include "autotest.h"
#include "core/log.h"
#include "ui/stick_view.h"
#include "ui/ui_style.h"

#define FINGER 93
#define FRAME_S (1.0 / 60.0)

static int failf(StickTest *t, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(t->why, sizeof(t->why), fmt, ap);
    va_end(ap);
    return -1;
}

static void next(StickTest *t, double now, int stage)
{
    t->stage = stage, t->sub = 0, t->at = now;
}

static void finger(MgApp *app, SDL_EventType type, float x, float y) { autotest_finger(app, type, FINGER, x, y); }

enum { S_START, S_RELEASE, S_TICKS, S_MARKER, S_RESPRING, S_REDUCED, S_SAFE, S_SHOTS, S_DONE };

int st_run(StickTest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double now)
{
    const StickView *v = p->stick;
    const TouchControls *tc = p->touch;
    if (!v || !tc || !p->playing) {
        return now > 30.0 + t->at && t->stage > 0 ? failf(t, "not in play") : 0;
    }
    const float r = p->stick_r, el = (float)(now - t->at);
    const float cx = app->width_pt * 0.2f, cy = app->height_pt * 0.5f;
    switch (t->stage) {
    case S_START:
        if (t->at == 0.0) {
            t->at = now;
        }
        if (el > 2.0f) {
            next(t, now, S_RELEASE);
        }
        return 0;
    case S_RELEASE: /* 1 + 2: zero input on the release tick; the drawn knob springs past centre and settles */
        if (t->sub == 0) {
            finger(app, SDL_EVENT_FINGER_DOWN, cx, cy);
            t->sub = 1, t->at = now;
        } else if (t->sub == 1 && el > 0.1f) {
            finger(app, SDL_EVENT_FINGER_MOTION, cx + 0.9f * r, cy);
            t->sub = 2, t->at = now;
        } else if (t->sub == 2 && el > 0.3f) {
            finger(app, SDL_EVENT_FINGER_UP, cx + 0.9f * r, cy);
            t->sub = 3, t->at = now;
            t->min_dot = 1.0f;
        } else if (t->sub == 3) {
            if (!v->releasing && !t->seen_release) {
                return el > 1.0f ? failf(t, "the release never started") : 0;
            }
            if (!t->seen_release) {
                t->seen_release = true;
                if (tc->stick_touch >= 0 || tc->move_x != 0.0f || tc->move_y != 0.0f) {
                    return failf(t, "input not zero on the release tick (%.3f, %.3f)", (double)tc->move_x, (double)tc->move_y);
                }
            }
            const float rel = v->rel_dx;
            if (fabsf(rel) > 1.0f) {
                t->min_dot = mg_minf(t->min_dot, v->knob_off_x / rel);
            }
            if (v->now - v->t_release < k_ui.stick_release_s + FRAME_S) {
                return 0;
            }
            const float off = sqrtf(v->knob_off_x * v->knob_off_x + v->knob_off_y * v->knob_off_y);
            if (off > 0.5f) {
                return failf(t, "the drawn knob is %.2f pt from centre %.0f ms after the release", (double)off, (v->now - v->t_release) * 1000.0);
            }
            if (t->min_dot > -0.01f) {
                return failf(t, "the knob never passed beyond centre (min %.3f of the release offset)", (double)t->min_dot);
            }
            mg_log_info("autotest stick: release: input zero on the release tick; overshoot %.0f %%; at centre by 420 ms + 1 frame", (double)(-t->min_dot * 100.0f));
            next(t, now, S_TICKS);
        }
        return 0;
    case S_TICKS: { /* 3: once per crossing, 0.70 R re-arm */
        static const struct { double at; float k; } path[] = {
            {0.00, 0.0f}, {0.10, 0.60f}, {0.25, 0.80f}, {0.40, 0.72f}, {0.45, 0.78f}, {0.50, 0.72f}, {0.55, 0.78f}, {0.60, 0.72f}, {0.65, 0.78f},
            {0.70, 0.72f}, {0.75, 0.78f}, {0.80, 0.72f}, {0.85, 0.78f}, {0.90, 0.72f}, {0.95, 0.78f}, {1.00, 0.72f}, {1.05, 0.78f}, {1.10, 0.72f},
            {1.15, 0.78f}, {1.20, 0.60f}, {1.70, 0.80f}, {1.90, -1.0f}};
        const int n = (int)(sizeof(path) / sizeof(path[0]));
        if (t->sub == 0) {
            if (el < 0.5f) {
                return 0;
            }
            t->ticks0 = v->ticks;
            t->t0 = now;
            finger(app, SDL_EVENT_FINGER_DOWN, cx, cy);
            t->sub = 1;
            return 0;
        }
        if (t->sub < n) {
            if (now - t->t0 >= path[t->sub].at) {
                if (path[t->sub].k < 0.0f) {
                    finger(app, SDL_EVENT_FINGER_UP, cx, cy);
                } else {
                    finger(app, SDL_EVENT_FINGER_MOTION, cx + path[t->sub].k * r, cy);
                }
                if (t->sub == 19 && v->ticks - t->ticks0 != 1) { /* the jitter is over: still one tick */
                    return failf(t, "%d ticks after one crossing and the 0.72-0.78 R jitter (want 1)", v->ticks - t->ticks0);
                }
                t->sub++;
            }
            return 0;
        }
        if (now - t->t0 < 2.3) {
            return 0;
        }
        if (v->ticks - t->ticks0 != 2) {
            return failf(t, "%d full-speed ticks, want 2 (one, none in the jitter, one after 0.6 R)", v->ticks - t->ticks0);
        }
        mg_log_info("autotest stick: full speed: 1 tick on crossing, 0 in the jitter, 1 after dropping to 0.6 R");
        next(t, now, S_MARKER);
        return 0;
    }
    case S_MARKER: /* 4: a marker tap in the stick zone: the stick never pops */
        if (t->sub == 0) {
            if (!t->marker_placed) {
                out->add_marker_left = true;
                t->marker_placed = true;
                t->at = now;
                return 0;
            }
            if (el < 1.0f) {
                return 0;
            }
            int found = -1;
            for (int i = 0; i < tc->target_count; i++) {
                if (tc->targets[i].x < app->width_pt * 0.45f && tc->targets[i].x > 0.0f && tc->targets[i].y > 60.0f && tc->targets[i].y < app->height_pt) {
                    found = i;
                }
            }
            if (found < 0) {
                return el > 4.0f ? failf(t, "no marker tap target in the stick zone") : 0;
            }
            t->cx = tc->targets[found].x, t->cy = tc->targets[found].y;
            t->pops0 = v->pops;
            finger(app, SDL_EVENT_FINGER_DOWN, t->cx, t->cy);
            t->sub = 1, t->at = now;
        } else if (t->sub == 1 && el > 0.08f) {
            finger(app, SDL_EVENT_FINGER_UP, t->cx, t->cy);
            t->sub = 2, t->at = now;
        } else if (t->sub == 2) {
            if (tc->stick_touch >= 0 || v->pops != t->pops0) {
                return failf(t, "a marker tap made the stick pop");
            }
            if (el > 0.5f) {
                mg_log_info("autotest stick: a marker tap in the stick zone: no stick, no pop");
                next(t, now, S_RESPRING);
            }
        }
        return 0;
    case S_RESPRING: /* 5: a touch during the spring starts from the drawn knob */
        if (t->sub == 0 && el > 0.8f) {
            finger(app, SDL_EVENT_FINGER_DOWN, cx, cy);
            t->sub = 1, t->at = now;
        } else if (t->sub == 1 && el > 0.1f) {
            finger(app, SDL_EVENT_FINGER_MOTION, cx + 0.9f * r, cy);
            t->sub = 2, t->at = now;
        } else if (t->sub == 2 && el > 0.25f) {
            finger(app, SDL_EVENT_FINGER_UP, cx + 0.9f * r, cy);
            t->sub = 3, t->at = now;
        } else if (t->sub == 3 && v->releasing && v->now - v->t_release > 0.12) {
            t->cx = v->knob_x, t->cy = v->knob_y; /* the thumb comes back down where it sees the knob */
            finger(app, SDL_EVENT_FINGER_DOWN, t->cx, t->cy);
            t->last_x = v->knob_x, t->last_y = v->knob_y;
            t->max_jump = 0.0f;
            t->sub = 4, t->at = now;
        } else if (t->sub == 4) {
            const float dx = v->knob_x - t->last_x, dy = v->knob_y - t->last_y;
            t->max_jump = mg_maxf(t->max_jump, sqrtf(dx * dx + dy * dy));
            t->last_x = v->knob_x, t->last_y = v->knob_y;
            if (el > 0.4f) {
                finger(app, SDL_EVENT_FINGER_UP, t->cx, t->cy);
                if (t->max_jump > 6.0f) {
                    return failf(t, "the drawn knob jumped %.1f pt between frames after a touch during the spring", (double)t->max_jump);
                }
                mg_log_info("autotest stick: touch during the spring: largest frame step %.2f pt", (double)t->max_jump);
                next(t, now, S_REDUCED);
            }
        }
        return 0;
    case S_REDUCED: /* 6: no scale other than 1; home in 100 ms */
        if (t->sub == 0) {
            p->uitest->reduced = true;
            if (el > 0.8f) {
                finger(app, SDL_EVENT_FINGER_DOWN, cx, cy);
                t->sub = 1, t->at = now;
            }
            return 0;
        }
        if ((tc->stick_touch >= 0 || v->releasing) && (fabsf(v->base_scale - 1.0f) > 1e-4f || fabsf(v->knob_sx - 1.0f) > 1e-4f || fabsf(v->knob_sy - 1.0f) > 1e-4f)) {
            return failf(t, "reduced motion: base %.3f, knob (%.3f, %.3f)", (double)v->base_scale, (double)v->knob_sx, (double)v->knob_sy);
        }
        if (t->sub == 1 && el > 0.15f) {
            finger(app, SDL_EVENT_FINGER_MOTION, cx + 0.9f * r, cy);
            t->sub = 2, t->at = now;
        } else if (t->sub == 2 && el > 0.3f) {
            finger(app, SDL_EVENT_FINGER_UP, cx + 0.9f * r, cy);
            t->sub = 3, t->at = now;
        } else if (t->sub == 3 && v->releasing && v->now - v->t_release >= k_ui.stick_reduced_s + FRAME_S) {
            const float off = sqrtf(v->knob_off_x * v->knob_off_x + v->knob_off_y * v->knob_off_y);
            if (off > 0.5f) {
                return failf(t, "reduced motion: the knob is %.2f pt from home after 100 ms", (double)off);
            }
            p->uitest->reduced = false;
            mg_log_info("autotest stick: reduced motion: scale 1 throughout, home in 100 ms");
            next(t, now, S_SAFE);
        } else if (t->sub == 3 && el > 1.0f) {
            return failf(t, "reduced motion: no release seen");
        }
        return 0;
    case S_SAFE: { /* 7 */
        const float s = r / k_ui.stick_design_r, lip = k_ui.stick_lip_off * s;
        const float x0 = p->stick_rest_x - r, x1 = p->stick_rest_x + r, y0 = p->stick_rest_y - r, y1 = p->stick_rest_y + r + lip;
        if (x0 < app->safe_left - 0.5f || y0 < app->safe_top - 0.5f || x1 > app->width_pt - app->safe_right + 0.5f || y1 > app->height_pt - app->safe_bottom + 0.5f) {
            return failf(t, "the idle stick (%.0f..%.0f, %.0f..%.0f) leaves the safe rect", (double)x0, (double)x1, (double)y0, (double)y1);
        }
        mg_log_info("autotest stick: the idle stick (%.0f..%.0f, %.0f..%.0f) is inside the safe rect (insets l %.0f b %.0f)", (double)x0, (double)x1, (double)y0,
                    (double)y1, (double)app->safe_left, (double)app->safe_bottom);
        next(t, now, S_SHOTS);
        return 0;
    }
    case S_SHOTS: /* 8 */
        if (t->sub == 0 && el > 1.2f) {
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "stick_idle");
            t->sub = 1, t->at = now;
        } else if (t->sub == 1 && el > 0.3f) {
            finger(app, SDL_EVENT_FINGER_DOWN, cx, cy);
            finger(app, SDL_EVENT_FINGER_MOTION, cx + 0.4f * r, cy - 0.2f * r);
            t->sub = 2, t->at = now;
        } else if (t->sub == 2 && el > 0.4f) {
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "stick_touched");
            t->sub = 3, t->at = now;
        } else if (t->sub == 3 && el > 0.2f) {
            finger(app, SDL_EVENT_FINGER_MOTION, cx + 0.85f * r, cy - 0.45f * r);
            t->sub = 4, t->at = now;
        } else if (t->sub == 4 && el > 0.4f) {
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "stick_full");
            t->sub = 5, t->at = now;
        } else if (t->sub == 5 && el > 0.2f) {
            finger(app, SDL_EVENT_FINGER_UP, cx + 0.85f * r, cy - 0.45f * r);
            t->sub = 6, t->at = now;
        } else if (t->sub == 6 && v->releasing && v->now - v->t_release > 0.13) {
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "stick_spring");
            next(t, now, S_DONE);
        }
        return 0;
    default:
        return el > 1.0f ? 1 : 0;
    }
}
