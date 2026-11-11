#include "autotest_book_ui.h"

#include <math.h>
#include <stdarg.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "autotest.h"
#include "book_pack.h"
#include "book_progress.h"
#include "core/log.h"
#include "core/prof.h"
#include "script_player.h"
#include "ui/ui_style.h"

enum { OP_TAP = 1, OP_DRAG, OP_WAIT };
#define BT_FINGER 61
#define SETTLE_S 0.7 /* a step's card and rows settle (the modal enters in 550 ms) */

/* an injected touch; SDL's event queue grows its free list on a burst of pushes: the test's own input, not the game's */
static void finger(MgApp *app, SDL_EventType type, SDL_FingerID id, float x, float y)
{
    prof_allocs_exempt(true);
    autotest_finger(app, type, id, x, y);
    prof_allocs_exempt(false);
}

static const char *const k_type[SP_TYPES] = {"simplu", "multiplu", "card", "cloze", "sort", "match", "order", "label"};

static int failf(BookUiTest *t, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(t->why, sizeof(t->why), fmt, ap);
    va_end(ap);
    return -1;
}

static void op(BookUiTest *t, int kind, const char *a, const char *b)
{
    if (t->n_ops >= BT_OPS) {
        return;
    }
    BtOp *o = &t->ops[t->n_ops++];
    o->kind = kind;
    SDL_snprintf(o->a, sizeof(o->a), "%s", a ? a : "");
    SDL_snprintf(o->b, sizeof(o->b), "%s", b ? b : "");
}

static void opf(BookUiTest *t, int kind, const char *fa, int ia, const char *fb, int ib)
{
    char a[24], b[24] = "";
    SDL_snprintf(a, sizeof(a), fa, ia);
    if (fb) {
        SDL_snprintf(b, sizeof(b), fb, ib);
    }
    op(t, kind, a, b);
}

static const mgpk_opt_t *opt(const ScriptPlayer *sp, int k) { return book_opt(sp->pack, sp->it->first_opt + (uint32_t)k); }
static const char *str_of(const ScriptPlayer *sp, uint32_t off) { return book_pack_str(sp->pack, off); }

/* the touches that answer the item on screen, right or (t->wrong) wrong; false when the item cannot be answered that way */
static bool build_ops(BookUiTest *t, const ScriptPlayer *sp)
{
    t->n_ops = t->op = t->sub = 0;
    const bool w = t->wrong;
    switch (sp->type) {
    case SP_SIMPLU:
        for (int k = 0; k < sp->n; k++) {
            if ((opt(sp, k)->ok != 0) != w) {
                opf(t, OP_TAP, "sp_opt%d", k, NULL, 0);
                return true;
            }
        }
        return false;
    case SP_MULTIPLU:
        for (int k = 0; k < sp->n; k++) {
            if ((opt(sp, k)->ok != 0) != w) {
                opf(t, OP_TAP, "sp_opt%d", k, NULL, 0);
                if (w) {
                    break; /* one wrong toggle is enough */
                }
            }
        }
        op(t, OP_TAP, "sp_confirm", NULL);
        return t->n_ops > 1;
    case SP_CARD:
        op(t, OP_TAP, "sp_flip", NULL);
        op(t, OP_WAIT, NULL, NULL);
        op(t, OP_TAP, w ? "sp_didnt" : "sp_knew", NULL);
        return true;
    case SP_CLOZE: {
        bool used[SP_CANDS] = {0};
        for (int g = 0; g < sp->n_gaps; g++) {
            const char *ans = str_of(sp, opt(sp, g)->text);
            int pick = -1;
            for (int c = 0; c < sp->n_cands && pick < 0; c++) {
                const bool same = strcmp(str_of(sp, book_opt(sp->pack, (uint32_t)sp->cand_opt[c])->text), ans) == 0;
                pick = !used[c] && (same != (w && g == 0)) ? c : -1;
            }
            if (pick < 0) {
                return false;
            }
            used[pick] = true;
            opf(t, OP_TAP, "sp_cand%d", pick, NULL, 0);
        }
        op(t, OP_TAP, "sp_confirm", NULL);
        return true;
    }
    case SP_SORT:
        if (sp->n_groups < 2) {
            return false;
        }
        for (int e = 0; e < sp->n; e++) {
            const int g = w && e == 0 ? (sp->entry_group[e] + 1) % sp->n_groups : sp->entry_group[e];
            if (e == 0) {
                opf(t, OP_DRAG, "sp_ent%d", e, "sp_bin%d", g);
            } else {
                opf(t, OP_TAP, "sp_ent%d", e, NULL, 0);
                opf(t, OP_TAP, "sp_bin%d", g, NULL, 0);
            }
        }
        op(t, OP_TAP, "sp_confirm", NULL);
        return true;
    case SP_MATCH:
        for (int i = 0; i < sp->n; i++) {
            opf(t, OP_TAP, "sp_ml%d", i, NULL, 0);
            opf(t, OP_TAP, "sp_mr%d", w ? (i + 1) % sp->n : i, NULL, 0);
        }
        op(t, OP_TAP, "sp_confirm", NULL);
        return sp->n > 1 || !w;
    case SP_ORDER: {
        if (!w) { /* the moves the player makes, simulated on a copy: bring group p to position p, p = 0, 1, ... */
            uint8_t perm[SP_OPTS];
            memcpy(perm, sp->perm, sizeof(perm));
            bool dragged = false;
            for (int p = 0; p < sp->n; p++) {
                int q = p;
                while (q < sp->n && opt(sp, perm[q])->group != p) {
                    q++;
                }
                if (q >= sp->n || q == p) {
                    continue;
                }
                char a[24], b[24];
                SDL_snprintf(a, sizeof(a), "sp_ord%d", p);
                SDL_snprintf(b, sizeof(b), "sp_ord%d", opt(sp, perm[p])->group);
                if (!dragged) {
                    op(t, OP_DRAG, a, b);
                    dragged = true;
                } else {
                    op(t, OP_TAP, a, NULL);
                    op(t, OP_TAP, b, NULL);
                }
                const uint8_t v = perm[q];
                memmove(&perm[p + 1], &perm[p], (size_t)(q - p));
                perm[p] = v;
            }
        }
        op(t, OP_TAP, "sp_confirm", NULL); /* wrong: the shuffled order as it is (never the right one) */
        return true;
    }
    case SP_LABEL:
        for (int k = 0; k < sp->n; k++) {
            const int box = w ? (k + 1) % sp->n : k;
            if (k == 0) {
                opf(t, OP_DRAG, "sp_lab%d", k, "sp_box%d", box);
            } else {
                opf(t, OP_TAP, "sp_lab%d", k, NULL, 0);
                opf(t, OP_TAP, "sp_box%d", box, NULL, 0);
            }
        }
        op(t, OP_TAP, "sp_confirm", NULL);
        return sp->n > 1 || !w;
    default:
        return false;
    }
}

/* scroll the card's body one step in t->dir (a drag along its left edge, clear of every chip); 1 when done */
static int scroll_step(BookUiTest *t, MgApp *app, const AutotestProbe *p, double now)
{
    float bx, by, bw, bh;
    if (!ui_rect_of(p->ui, "sp_body", &bx, &by, &bw, &bh)) {
        return failf(t, "the card's body is not on screen");
    }
    const float x = bx - 3.0f, y0 = by + bh * (t->dir > 0 ? 0.85f : 0.15f), y1 = by + bh * (t->dir > 0 ? 0.25f : 0.75f);
    switch (t->sub) {
    case 10:
        t->last_off = p->sp->scroll.offset;
        finger(app, SDL_EVENT_FINGER_DOWN, BT_FINGER, x, y0);
        t->motion = 0, t->sub = 11;
        return 0;
    case 11:
        t->motion++;
        finger(app, SDL_EVENT_FINGER_MOTION, BT_FINGER, x, y0 + (y1 - y0) * (float)t->motion / 6.0f);
        if (t->motion >= 6) {
            t->sub = 12, t->at = now;
        }
        return 0;
    case 12:
        if (now - t->at >= 0.05) {
            finger(app, SDL_EVENT_FINGER_UP, BT_FINGER, x, y1);
            t->sub = 13, t->at = now;
        }
        return 0;
    default:
        if (now - t->at < 0.2) {
            return 0;
        }
        mg_log_info("autotest book_ui: scrolled for %s: offset %.0f -> %.0f (content %.0f)", t->ops[t->op].a, (double)t->last_off, (double)p->sp->scroll.offset,
                    (double)p->sp->scroll.content);
        if (fabsf(p->sp->scroll.offset - t->last_off) < 1.0f) { /* at an end: the other way */
            t->dir = t->dir > 0 ? -1 : 1;
        }
        if (++t->scrolls > 16) {
            return failf(t, "%s: widget %s never came into view", k_type[p->sp->type >= 0 ? p->sp->type : 0], t->ops[t->op].a);
        }
        t->sub = 0;
        return 1;
    }
}

/* runs ops[op]; 1 when it is done */
static int run_op(BookUiTest *t, MgApp *app, const AutotestProbe *p, double now)
{
    BtOp *o = &t->ops[t->op];
    if (t->sub >= 10 && t->sub < 20) {
        const int r = scroll_step(t, app, p, now);
        return r < 0 ? r : 0;
    }
    switch (o->kind) {
    case OP_WAIT:
        if (t->sub == 0) {
            t->sub = 1, t->at = now;
        }
        if (now - t->at >= 0.5) {
            t->sub = 0;
            return 1;
        }
        return 0;
    case OP_TAP:
        if (t->sub == 0) {
            float x, y;
            if (!ui_find(p->ui, o->a, &x, &y)) {
                t->sub = 10;
                return 0;
            }
            t->scrolls = 0;
            t->fx = x, t->fy = y;
            finger(app, SDL_EVENT_FINGER_DOWN, BT_FINGER, x, y);
            t->sub = 1, t->at = now;
        } else if (t->sub == 1 && now - t->at >= 0.05) {
            finger(app, SDL_EVENT_FINGER_UP, BT_FINGER, t->fx, t->fy);
            t->sub = 2, t->at = now;
        } else if (t->sub == 2 && now - t->at >= 0.2) {
            t->sub = 0;
            return 1;
        }
        return 0;
    case OP_DRAG:
        if (t->sub == 0) {
            float ax, ay, bx, by;
            const bool fa = ui_find(p->ui, o->a, &ax, &ay), fb = ui_find(p->ui, o->b, &bx, &by);
            if (!fa) {
                t->sub = 10;
                return 0;
            }
            if (!fb) { /* both are not on screen at once (a phone): the same move by taps, and say so */
                mg_log_info("autotest book_ui: %s and %s not on screen together: tap, then tap", o->a, o->b);
                if (t->n_ops < BT_OPS) {
                    memmove(&t->ops[t->op + 1], &t->ops[t->op], sizeof(BtOp) * (size_t)(t->n_ops - t->op));
                    t->n_ops++;
                    t->ops[t->op].kind = OP_TAP, t->ops[t->op + 1].kind = OP_TAP;
                    SDL_snprintf(t->ops[t->op + 1].a, sizeof(t->ops[t->op + 1].a), "%s", o->b);
                }
                return 0;
            }
            t->scrolls = 0;
            t->fx = ax, t->fy = ay, t->tx = bx, t->ty = by;
            finger(app, SDL_EVENT_FINGER_DOWN, BT_FINGER, ax, ay);
            t->sub = 21, t->motion = 0, t->at = now;
        } else if (t->sub == 21 && now - t->at >= 0.05) {
            t->motion++;
            const float k = (float)t->motion / 10.0f;
            finger(app, SDL_EVENT_FINGER_MOTION, BT_FINGER, t->fx + (t->tx - t->fx) * k, t->fy + (t->ty - t->fy) * k);
            if (t->motion == 1 && p->sp->drag < 0) {
                /* the first motion is checked on the next frame */
            }
            if (t->motion >= 10) {
                t->sub = 22, t->at = now;
            }
        } else if (t->sub == 22 && now - t->at >= 0.08) {
            if (p->sp->drag < 0) {
                return failf(t, "%s: a drag from %s did not pick it up", k_type[p->sp->type], o->a);
            }
            finger(app, SDL_EVENT_FINGER_UP, BT_FINGER, t->tx, t->ty);
            t->sub = 23, t->at = now;
        } else if (t->sub == 23 && now - t->at >= 0.25) {
            t->drags++;
            t->sub = 0;
            return 1;
        }
        return 0;
    default:
        return 1;
    }
}

/* every frame with the card settled: UI draws, allocations, targets */
static void frame_checks(BookUiTest *t, MgApp *app, const AutotestProbe *p)
{
    const ScriptPlayer *sp = p->sp;
    t->checked++;
    const float l = app->safe_left, tp = app->safe_top, r = app->width_pt - app->safe_right, b = app->height_pt - app->safe_bottom;
    for (int i = sp->seen_from; i < sp->seen_to && i < p->ui->seen_count; i++) {
        const float x = p->ui->seen[i].x, y = p->ui->seen[i].y, w = p->ui->seen[i].w, h = p->ui->seen[i].h;
        if (p->ui->seen[i].rect_only || p->ui->seen[i].partial) {
            continue;
        }
        if (w < k_ui.target_min - 0.5f || h < k_ui.target_min - 0.5f) {
            if (t->small++ < 5) {
                mg_log_info("autotest book_ui: a %.0f x %.0f pt target at %.0f, %.0f (%s)", (double)w, (double)h, (double)x, (double)y,
                            sp->type >= 0 ? k_type[sp->type] : "teach");
            }
        }
        if (x < l - 0.5f || y < tp - 0.5f || x + w > r + 0.5f || y + h > b + 0.5f) {
            if (t->outside++ < 5) {
                mg_log_info("autotest book_ui: a target outside the safe rect: %.0f, %.0f, %.0f x %.0f", (double)x, (double)y, (double)w, (double)h);
            }
        }
    }
}

static bool facts_of_item(const ScriptPlayer *sp, const BookProgress *pr, int k) { return book_progress_fact(pr, (int)book_ref(sp->pack, sp->it->first_fact_ref + (uint32_t)k)); }

int bu_run(BookUiTest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double now)
{
    ScriptPlayer *sp = p->sp;
    if (t->t0 == 0.0) {
        t->t0 = now;
        const char *win = SDL_getenv("MG_WINDOW");
        SDL_snprintf(t->win, sizeof(t->win), "%s", win && win[0] ? win : "default");
    }
    if (now - t->t0 > 900.0) { /* 16 runs; the real pack's scripts take ~40 s each */
        return failf(t, "timed out in run %d", t->run);
    }
    if (sp && sp->active && t->stage >= 2) { /* every frame with the card up: allocations and UI draws; settled and still: targets */
        t->frames++;
        t->worst_ui_draws = p->ui_draws > t->worst_ui_draws ? p->ui_draws : t->worst_ui_draws;
        if (sp->allocs) { /* the card's own; the frame's others (the network thread's events, the test's input) are logged */
            t->allocs += sp->allocs;
            mg_log_info("autotest book_ui: the card made %u heap allocations in a frame (step %d, %s)", sp->allocs, sp->step, sp->type >= 0 ? k_type[sp->type] : "teach");
        }
        if (p->frame_allocs > sp->allocs) {
            t->other_allocs += p->frame_allocs - sp->allocs;
        }
        if (now - t->step_at >= SETTLE_S && sp->drag < 0) {
            frame_checks(t, app, p);
        }
    }
    switch (t->stage) {
    case 0: { /* in the institute, the pack open: a script for every item type */
        if (!p->playing || now - t->t0 < 3.0) {
            return 0;
        }
        if (!p->book || !p->book->ok || !sp) {
            return failf(t, "no content pack");
        }
        if (!p->progress) {
            return failf(t, "no progress file open");
        }
        for (int i = 0; i < SP_TYPES; i++) {
            t->script[i] = -1;
        }
        for (uint32_t s = 0; s < p->book->script_count; s++) {
            const mgpk_script_t *sc = book_script(p->book, s);
            for (uint32_t k = 0; k < sc->step_count; k++) {
                const mgpk_step_t *st = book_step(p->book, sc->first_step + k);
                const mgpk_item_t *it = st && st->stage == SP_CHECK ? book_item(p->book, st->item) : NULL;
                if (it && it->type < SP_TYPES && t->script[it->type] < 0) {
                    t->script[it->type] = (int)s;
                }
            }
        }
        for (int i = 0; i < SP_TYPES; i++) {
            if (t->script[i] < 0) {
                return failf(t, "no script checks a %s item", k_type[i]);
            }
        }
        book_progress_reset(p->progress, (double)SDL_GetTicks()); /* this test's own profile: every fact unset */
        mg_log_info("autotest book_ui: window %s, scripts per type found, progress reset", t->win);
        const char *from = SDL_getenv("MG_BOOK_UI_FROM"); /* debugging: start at run N (2 per type, wrong first) */
        t->run = from ? SDL_atoi(from) : 0;
        t->dir = 1;
        prof_run_reset(); /* the allocation sites logged at the end are this test's */
        t->stage = 1;
        return 0;
    }
    case 1: { /* a run: type run / 2, the wrong answer first */
        const int type = t->run / 2;
        t->wrong = t->run % 2 == 0;
        if (type == SP_LABEL && !t->wrong) {
            sp->fig_resident = -1; /* the figure goes up again inside a frame: its upload must not allocate either */
        }
        if (!script_player_open(sp, p->book, (uint32_t)t->script[type], "Dr. Ana Test")) {
            return failf(t, "the player did not open script %d", t->script[type]);
        }
        t->step_seen = -1;
        t->stage = 2;
        return 0;
    }
    case 2: {
        if (!sp->active) { /* the run's script ended */
            if (!sp->finished) {
                return failf(t, "%s: the script ended unfinished", k_type[t->run / 2]);
            }
            mg_log_info("autotest book_ui: %s answered %s: done (%d / %d checks right)", k_type[t->run / 2], t->wrong ? "wrong" : "right", sp->checks_right, sp->checks);
            t->run++;
            t->stage = t->run >= 2 * SP_TYPES ? 9 : 1;
            return 0;
        }
        if (sp->step != t->step_seen) {
            t->step_seen = sp->step;
            t->at = t->step_at = now;
            t->sub = t->op = t->n_ops = 0;
            t->stage = 3;
        }
        return 0;
    }
    case 3: /* settle, then the step's picture and its moves */
        if (now - t->at < SETTLE_S) {
            return 0;
        }
        if (sp->stage == SP_TEACH || !sp->it) {
            if (t->run == 0 && sp->step == 0) {
                SDL_snprintf(out->screenshot, sizeof(out->screenshot), "book_%s_teach", t->win);
            }
            op(t, OP_TAP, "sp_next", NULL);
            t->stage = 5;
            return 0;
        }
        if (sp->type == SP_LABEL || sp->fig_shown >= 0) {
            if (!sp->fig_drawn && !sp->fig_placeholder) {
                return failf(t, "label: the figure is neither drawn nor a placeholder");
            }
            t->figs += sp->fig_drawn ? 1 : 0;
            t->placeholders += sp->fig_placeholder ? 1 : 0;
        }
        memset(t->facts_before, 0, sizeof(t->facts_before));
        for (int k = 0; k < sp->it->n_facts && k < 512; k++) {
            t->facts_before[k >> 3] |= facts_of_item(sp, p->progress, k) ? (uint8_t)(1u << (k & 7)) : 0u;
        }
        if (!build_ops(t, sp)) {
            return failf(t, "%s: cannot answer it %s", k_type[sp->type], t->wrong ? "wrong" : "right");
        }
        if (!t->wrong) {
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "book_%s_%s", t->win, k_type[sp->type]);
        }
        t->at = now;
        t->stage = 4;
        return 0;
    case 4: { /* the moves, then the verdict, the facts, the picture after */
        if (now - t->at < 0.3) {
            return 0;
        }
        if (t->op < t->n_ops) {
            const int r = run_op(t, app, p, now);
            if (r < 0) {
                return -1;
            }
            t->op += r;
            return 0;
        }
        if (!sp->answered) {
            return failf(t, "%s: not answered after its moves", k_type[sp->type]);
        }
        if (sp->right == t->wrong) {
            return failf(t, "%s: answered %s, wanted %s", k_type[sp->type], sp->right ? "right" : "wrong", t->wrong ? "wrong" : "right");
        }
        if (sp->stage == SP_CHECK) {
            for (int k = 0; k < sp->it->n_facts && k < 512; k++) {
                const bool before = (t->facts_before[k >> 3] >> (k & 7)) & 1u, now_set = facts_of_item(sp, p->progress, k);
                if (!t->wrong && !now_set) {
                    return failf(t, "%s: answered right but fact %u is not set", k_type[sp->type], book_ref(sp->pack, sp->it->first_fact_ref + (uint32_t)k));
                }
                if (t->wrong && !before && now_set) {
                    return failf(t, "%s: answered wrong but fact %u was set", k_type[sp->type], book_ref(sp->pack, sp->it->first_fact_ref + (uint32_t)k));
                }
            }
            mg_log_info("autotest book_ui: %s answered %s: %d facts %s (ok)", k_type[sp->type], t->wrong ? "wrong" : "right", sp->it->n_facts,
                        t->wrong ? "left unset" : "set");
        }
        if (SDL_strcmp(t->win, "default") == 0 || !t->wrong) {
            SDL_snprintf(out->screenshot, sizeof(out->screenshot), "book_%s_%s_%s", t->win, k_type[sp->type], t->wrong ? "bad" : "ok");
        }
        t->n_ops = t->op = t->sub = 0;
        op(t, OP_TAP, "sp_next", NULL);
        t->at = now;
        t->stage = 5;
        return 0;
    }
    case 5: { /* Continue / Done */
        if (now - t->at < 0.4) {
            return 0;
        }
        const int r = run_op(t, app, p, now);
        if (r < 0) {
            return -1;
        }
        if (r > 0) {
            t->stage = 2;
        }
        return 0;
    }
    default: {
        mg_log_info("autotest book_ui: window %s: %d runs, %u frames with the card (%u checked for targets), worst %u UI draws (max 3), %u heap allocations by the card (%u elsewhere in those frames), %d targets under 44 pt, "
                    "%d outside the safe rect, %d drags, figure drawn %d / placeholder %d",
                    t->win, t->run, t->frames, t->checked, t->worst_ui_draws, t->allocs, t->other_allocs, t->small, t->outside, t->drags, t->figs, t->placeholders);
        if (t->worst_ui_draws > 3) {
            return failf(t, "%u UI draws in a frame with the card up (max 3)", t->worst_ui_draws);
        }
        for (int i = 0; i < 12; i++) {
            const char *scope;
            uint32_t bytes;
            if (prof_alloc_site(i, &scope, &bytes)) {
                mg_log_info("autotest book_ui: allocation %d: %u bytes in %s", i, bytes, scope ? scope : "?");
            }
        }
        if (t->allocs) {
            return failf(t, "%u heap allocations in frames with the card up", t->allocs);
        }
        if (t->small || t->outside) {
            return failf(t, "%d targets under 44 pt, %d outside the safe rect", t->small, t->outside);
        }
        if (t->drags < 2) { /* sort and label always (their chips and targets share the screen); order when its rows fit */
            return failf(t, "only %d real drags (sort, order, label)", t->drags);
        }
        return 1;
    }
    }
}
