#include "script_player.h"

#include <math.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "core/assets.h"
#include "core/log.h"
#include "core/prof.h"
#include "strings.h"
#include "ui/haptics.h"
#include "ui/ui_widgets.h"

#define PAD 20.0f        /* card edge to content */
#define HEAD_H 62.0f     /* header band */
#define GAP 10.0f        /* between blocks */
#define T_STEM 17.0f
#define T_TEXT 16.0f
#define T_NOTE 14.0f
#define DRAG_START 8.0f  /* pt a finger moves on a chip before it is a drag (the scroll takes 10) */

static UiColor C(uint32_t c) { return ui_rgba(c); }
static UiColor fade(UiColor c, float a) { return (UiColor){c.r, c.g, c.b, (uint8_t)((float)c.a * a)}; }
static const char *S(const ScriptPlayer *sp, uint32_t off) { return book_pack_str(sp->pack, off); }
static const mgpk_opt_t *opt_of(const ScriptPlayer *sp, int k) { return book_opt(sp->pack, sp->it->first_opt + (uint32_t)k); }

static uint32_t xs(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13, x ^= x >> 17, x ^= x << 5;
    return *s = x;
}

/* a shuffled 0..n-1 that is not the identity (n > 1): order and match must never start solved */
static void shuffle(uint8_t *p, int n, uint32_t seed)
{
    for (int i = 0; i < n; i++) {
        p[i] = (uint8_t)i;
    }
    for (int tries = 0; tries < 8; tries++) {
        for (int i = n - 1; i > 0; i--) {
            const int j = (int)(xs(&seed) % (uint32_t)(i + 1));
            const uint8_t t = p[i];
            p[i] = p[j], p[j] = t;
        }
        bool id = true;
        for (int i = 0; i < n && id; i++) {
            id = p[i] == i;
        }
        if (!id || n < 2) {
            return;
        }
    }
    const uint8_t t = p[0]; /* still the identity: one swap */
    p[0] = p[1], p[1] = t;
}

/* ------------------------------------------------------------------ text layout (cached per step) */

static void relayout(ScriptPlayer *sp) { sp->n_blocks = 0, sp->n_lines = 0; }

static const SpBlock *wrap(ScriptPlayer *sp, const Ui *ui, const char *s, int face, float size, float w)
{
    const uint32_t len = (uint32_t)strlen(s);
    for (int i = 0; i < sp->n_blocks; i++) {
        const SpBlock *b = &sp->blocks[i];
        if (b->s == s && b->len == len && b->w == w && b->size == size && b->face == face) {
            return b;
        }
    }
    if (sp->n_blocks >= SP_BLOCKS || sp->n_lines >= SP_LINES) {
        relayout(sp); /* full: start over (this frame lays out only what it draws) */
    }
    SpBlock *b = &sp->blocks[sp->n_blocks++];
    const int room = SP_LINES - sp->n_lines;
    const int need = len ? text_wrap(ui_face(ui, face), s, len, size, w, &sp->lines[sp->n_lines], room) : 0;
    if (need > room) {
        mg_log_error("script player: %d lines for one text, room for %d", need, room);
    }
    *b = (SpBlock){s, len, w, size, face, sp->n_lines, need < room ? need : room};
    sp->n_lines += b->count;
    return b;
}

static float text_h(ScriptPlayer *sp, const Ui *ui, const char *s, int face, float size, float w)
{
    return (float)wrap(sp, ui, s, face, size, w)->count * text_line_height(ui_face(ui, face), size);
}

/* draws only the lines inside [v0, v1] (the body's view); returns the height */
static float text_block(ScriptPlayer *sp, Ui *ui, const char *s, int face, float size, UiColor c, float x, float y, float w, float v0, float v1)
{
    const SpBlock *b = wrap(sp, ui, s, face, size, w);
    const TextFont *f = ui_face(ui, face);
    const float lh = text_line_height(f, size);
    for (int i = 0; i < b->count; i++) {
        const float ly = y + (float)i * lh;
        const TextLine *ln = &sp->lines[b->first + i];
        if (ly + lh < v0 || ly > v1 || ln->len == 0) {
            continue;
        }
        text_draw_n(ui->gfx, f, x, ly, size, c, s + ln->start, ln->len);
    }
    return (float)b->count * lh;
}

/* ------------------------------------------------------------------ figures */

static void fig_want(ScriptPlayer *sp, const char *id);

void script_player_init(ScriptPlayer *sp, GfxContext *g)
{
    memset(sp, 0, sizeof(*sp));
    sp->gfx = g;
    sp->fig_resident = sp->fig_shown = -1;
    sp->skip_fact = -1;
    size_t fsize = 0;
    const void *fdata = book_pack_figures(&fsize);
    char why[96] = "";
    if (!fdata) { /* no pack, no figures: nothing to reserve */
        return;
    }
    if (!book_figs_open(&sp->figs, fdata, fsize, why, sizeof(why))) {
        mg_log_error("book: figures: %s", why);
        return;
    }
    sp->figs_ok = true;
    int mw = 0, mh = 0;
    for (int i = 0; i < sp->figs.count; i++) {
        BookFig f;
        if (book_figs_get(&sp->figs, i, &f)) {
            mw = f.w > mw ? f.w : mw, mh = f.h > mh ? f.h : mh;
        }
    }
    const bool gpu = mw > 0 && gfx_figure_reserve(g, mw, mh, sp->figs.format == BOOK_FIG_ASTC4 ? GFX_FIG_ASTC4 : GFX_FIG_RGBA8);
    if (gpu) { /* one upload now: the GPU layer's command buffer and fence pools are warm before the first figure in play */
        BookFig f0;
        if (book_figs_get(&sp->figs, 0, &f0)) {
            fig_want(sp, f0.id);
        }
        sp->fig_shown = -1;
    }
    mg_log_info("book: figures: %d, %s, largest %d x %d, %u bytes the largest%s", sp->figs.count, sp->figs.format == BOOK_FIG_ASTC4 ? "ASTC 4x4" : "RGBA8", mw,
                mh, book_figs_largest(&sp->figs), gpu ? "" : " (not on the GPU: placeholders)");
}

static void fig_want(ScriptPlayer *sp, const char *id)
{
    sp->fig_shown = sp->figs_ok && id && id[0] ? book_figs_find(&sp->figs, id) : -1;
    if (sp->fig_shown < 0 || sp->fig_shown == sp->fig_resident || !sp->gfx) {
        return;
    }
    BookFig f;
    book_figs_get(&sp->figs, sp->fig_shown, &f);
    const int fmt = sp->figs.format == BOOK_FIG_ASTC4 ? GFX_FIG_ASTC4 : GFX_FIG_RGBA8;
    sp->fig_resident = gfx_set_figure(sp->gfx, f.w, f.h, fmt, f.data, f.size) ? sp->fig_shown : -1;
}

/* the figure fitted into (x, y, maxw, maxh), framed; a placeholder box when it is not on the GPU. Returns the drawn rect. */
static void fig_draw(ScriptPlayer *sp, Ui *ui, float x, float y, float maxw, float maxh, float *out)
{
    BookFig f = {0};
    if (sp->fig_shown >= 0) {
        book_figs_get(&sp->figs, sp->fig_shown, &f);
    }
    const float aw = f.w > 0 ? (float)f.w : 4.0f, ah = f.h > 0 ? (float)f.h : 3.0f;
    float w = maxw, h = maxw * ah / aw;
    if (h > maxh) {
        h = maxh, w = maxh * aw / ah;
    }
    out[0] = x, out[1] = y, out[2] = w, out[3] = h;
    ui_shape(ui->gfx, &(UiShape){.x = x - 4.0f, .y = y - 4.0f, .w = w + 8.0f, .h = h + 8.0f, .radius = 10.0f, .color = C(k_ui.surface_tint)});
    const bool on = sp->fig_shown >= 0 && sp->fig_shown == sp->fig_resident && ui_figure_quad(ui->gfx, x, y, w, h, C(k_ui.white));
    if (on) {
        sp->fig_drawn = true;
    } else { /* no figure on the GPU (format unsupported, missing id): say so, never an empty frame */
        sp->fig_placeholder = true;
        ui_shape(ui->gfx, &(UiShape){.x = x, .y = y, .w = w, .h = h, .radius = 8.0f, .stroke = 2.0f, .dashes = 0.0f, .color = C(k_ui.secondary_lip)});
        ui_text_c(ui, TEXT_FACE_BOLD, x + w * 0.5f, y + h * 0.5f, T_NOTE, C(k_ui.ink_soft), str("sp_fig_missing"));
    }
}

/* ------------------------------------------------------------------ steps */

static const mgpk_step_t *step_of(const ScriptPlayer *sp, int k)
{
    const mgpk_script_t *sc = book_script(sp->pack, sp->script);
    return sc && k >= 0 && k < (int)sc->step_count ? book_step(sp->pack, sc->first_step + (uint32_t)k) : NULL;
}

static int count_gaps(const char *s)
{
    int n = 0;
    for (const char *p = s; (p = strstr(p, "____")) != NULL; n++) {
        while (*p == '_') {
            p++;
        }
    }
    return n;
}

static void build_cloze(ScriptPlayer *sp)
{
    const char *s = S(sp, sp->it->stem);
    size_t o = 0;
    const size_t cap = sizeof(sp->cloze_buf) - 1;
    int g = 0;
    while (*s && o < cap) {
        if (strncmp(s, "____", 4) == 0) {
            while (*s == '_') {
                s++;
            }
            const char *fill = g < sp->n_gaps && sp->gap_fill[g] >= 0 ? S(sp, book_opt(sp->pack, (uint32_t)sp->cand_opt[sp->gap_fill[g]])->text) : NULL;
            o += (size_t)SDL_snprintf(sp->cloze_buf + o, cap + 1 - o, fill ? "[%s]" : "______", fill ? fill : "");
            o = o > cap ? cap : o;
            g++;
            continue;
        }
        sp->cloze_buf[o++] = *s++;
    }
    sp->cloze_buf[o] = 0;
}

static void label_boxes(ScriptPlayer *sp)
{
    const char *fig = S(sp, opt_of(sp, 0)->aux);
    bool used[64] = {0};
    sp->crop_w = sp->crop_h = 0.0f;
    for (int i = 0; i < sp->n; i++) {
        int pick = -1, first_free = -1, k = 0;
        for (uint32_t r = 0; r < sp->pack->figlabel_count && k < 64; r++) {
            const mgpk_figlabel_t *fl = book_figlabel(sp->pack, r);
            if (strcmp(S(sp, fl->fig), fig) != 0) {
                continue;
            }
            sp->crop_w = fl->w, sp->crop_h = fl->h;
            if (!used[k] && first_free < 0) {
                first_free = k;
            }
            if (!used[k] && pick < 0 && strcmp(S(sp, fl->text), S(sp, opt_of(sp, i)->text)) == 0) {
                pick = k;
            }
            k++;
        }
        pick = pick >= 0 ? pick : first_free; /* a label with no box of its own text takes the next free one (the demo pack) */
        if (pick >= 0) {
            used[pick] = true;
            for (uint32_t r = 0, j = 0; r < sp->pack->figlabel_count; r++) {
                const mgpk_figlabel_t *fl = book_figlabel(sp->pack, r);
                if (strcmp(S(sp, fl->fig), fig) == 0 && (int)j++ == pick) {
                    sp->box[i][0] = fl->x0, sp->box[i][1] = fl->y0, sp->box[i][2] = fl->x1, sp->box[i][3] = fl->y1;
                }
            }
        } else { /* no box at all: a row of boxes along the bottom so the item still plays */
            sp->box[i][0] = (float)i / (float)sp->n, sp->box[i][1] = 0.85f, sp->box[i][2] = ((float)i + 0.9f) / (float)sp->n, sp->box[i][3] = 0.97f;
        }
    }
    if (sp->crop_w <= 0.0f) { /* boxes were made in 0..1 */
        sp->crop_w = sp->crop_h = 1.0f;
    }
    sp->n_boxes = sp->n;
    fig_want(sp, fig);
}

static void cloze_setup(ScriptPlayer *sp)
{
    const int gaps = count_gaps(S(sp, sp->it->stem));
    sp->n_gaps = gaps < 1 ? 1 : gaps;
    sp->n_gaps = sp->n_gaps > sp->n ? sp->n : sp->n_gaps;
    sp->n_gaps = sp->n_gaps > SP_GAPS ? SP_GAPS : sp->n_gaps;
    sp->n_cands = 0;
    /* the item's answers, then its siblings' (the topic's other cloze items; while there are fewer than 4 candidates, the
     * chapter's, then the pack's), no text twice */
    const mgpk_topic_t *tp = book_topic(sp->pack, sp->it->topic);
    const int want = sp->n_gaps + 3 < SP_CANDS ? sp->n_gaps + 3 : SP_CANDS;
    for (int pass = 0; pass < 4 && sp->n_cands < want; pass++) {
        for (uint32_t ii = 0; ii < sp->pack->item_count && sp->n_cands < (pass == 0 ? SP_CANDS : want); ii++) {
            const mgpk_item_t *x = book_item(sp->pack, ii);
            const mgpk_topic_t *xt = x ? book_topic(sp->pack, x->topic) : NULL;
            if (!x || x->type != SP_CLOZE || (pass == 0) != (ii == sp->item)) {
                continue;
            }
            if ((pass == 1 && x->topic != sp->it->topic) || (pass == 2 && (!xt || !tp || xt->chapter != tp->chapter || x->topic == sp->it->topic)) ||
                (pass == 3 && xt && tp && xt->chapter == tp->chapter)) {
                continue;
            }
            for (uint32_t k = 0; k < x->n_opts && sp->n_cands < SP_CANDS; k++) {
                const uint32_t row = x->first_opt + k;
                bool dup = false;
                for (int c = 0; c < sp->n_cands && !dup; c++) {
                    dup = strcmp(S(sp, book_opt(sp->pack, (uint32_t)sp->cand_opt[c])->text), S(sp, book_opt(sp->pack, row)->text)) == 0;
                }
                if (!dup) {
                    sp->cand_opt[sp->n_cands++] = (int)row;
                }
            }
        }
    }
    uint8_t p[SP_CANDS];
    shuffle(p, sp->n_cands, sp->item * 2654435761u + 11u);
    int tmp[SP_CANDS];
    for (int c = 0; c < sp->n_cands; c++) {
        tmp[c] = sp->cand_opt[p[c]];
    }
    memcpy(sp->cand_opt, tmp, sizeof(int) * (size_t)sp->n_cands);
    for (int g = 0; g < SP_GAPS; g++) {
        sp->gap_fill[g] = -1;
    }
    build_cloze(sp);
}

static void step_enter(ScriptPlayer *sp, double t)
{
    const mgpk_step_t *st = step_of(sp, sp->step);
    sp->stage = st ? st->stage : SP_TEACH;
    sp->it = NULL, sp->type = -1, sp->n = 0;
    sp->answered = sp->right = sp->flipped = false;
    sp->sel = 0, sp->selected = -1, sp->drag = -1, sp->n_drop = 0;
    sp->step_t = t;
    sp->scroll = (UiScroll){0};
    memset(sp->place, -1, sizeof(sp->place));
    memset(sp->drag_key, 0, sizeof(sp->drag_key));
    sp->fig_shown = -1;
    relayout(sp);
    if (!st) {
        return;
    }
    if (sp->stage == SP_TEACH) {
        for (uint32_t k = 0; k < st->n_atoms; k++) { /* the first atom with a figure */
            const mgpk_atom_t *a = book_atom(sp->pack, book_ref(sp->pack, st->first_atom_ref + k));
            if (a && a->fig != MGPK_NONE && S(sp, a->fig)[0]) {
                fig_want(sp, S(sp, a->fig));
                break;
            }
        }
        return;
    }
    sp->item = st->item;
    sp->it = book_item(sp->pack, st->item);
    if (!sp->it) {
        return;
    }
    sp->type = sp->it->type;
    sp->n = sp->it->n_opts < SP_OPTS ? sp->it->n_opts : SP_OPTS;
    const uint32_t seed = sp->item * 2654435761u + 7u;
    switch (sp->type) {
    case SP_SORT: { /* entries first, then the 2-3 group names (group 255) */
        int e = 0;
        sp->n_groups = 0;
        for (uint32_t k = 0; k < sp->it->n_opts; k++) {
            const mgpk_opt_t *o = book_opt(sp->pack, sp->it->first_opt + k);
            if (o->group == 255) {
                if (sp->n_groups < SP_GROUPS) {
                    sp->group_text[sp->n_groups++] = o->text;
                }
            } else if (e < SP_OPTS) {
                sp->entry_group[e++] = o->group;
            }
        }
        sp->n = e;
        shuffle(sp->perm, sp->n, seed);
        break;
    }
    case SP_MATCH:
    case SP_ORDER:
        shuffle(sp->perm, sp->n, seed);
        break;
    case SP_LABEL:
        shuffle(sp->perm, sp->n, seed);
        label_boxes(sp);
        break;
    case SP_CLOZE:
        cloze_setup(sp);
        break;
    default:
        break;
    }
}

/* lines of s wrapped at w (into the player's line buffer: called at load, before any card) */
static int lines_of(ScriptPlayer *sp, const TextFont *f, const char *s, float size, float w)
{
    const size_t n = strlen(s);
    return n ? text_wrap(f, s, n, size, w, sp->lines, SP_LINES) : 0;
}

void script_player_survey(ScriptPlayer *sp, const BookPack *pack, const TextFont *body, const TextFont *bold)
{
    SpWorst *w = &sp->worst;
    memset(w, 0, sizeof(*w));
    if (!pack || !pack->ok || !body || !bold) {
        return;
    }
    const Uint64 t0 = SDL_GetTicksNS();
    /* the narrowest card: the iPhone preset (844 x 390, 59 pt inset): safe 785, card margin 10, body padding PAD */
    w->width = 785.0f - 20.0f - 2.0f * PAD;
    const float row_w = w->width - 56.0f - 24.0f; /* an option row's text column (text_row with a box mark) */
    int score[SP_TYPES], long_score[SP_TYPES];
    for (int t = 0; t < SP_TYPES; t++) {
        w->most_item[t] = w->long_item[t] = MGPK_NONE;
        score[t] = long_score[t] = -1;
    }
    for (uint32_t i = 0; i < pack->item_count; i++) {
        const mgpk_item_t *it = book_item(pack, i);
        if (!it || it->type >= SP_TYPES) {
            continue;
        }
        const int t = it->type;
        int count = it->n_opts;
        if (t == SP_SORT) { /* entries, not the group names */
            count = 0;
            for (uint32_t k = 0; k < it->n_opts; k++) {
                count += book_opt(pack, it->first_opt + k)->group != 255 ? 1 : 0;
            }
            w->sort_groups = (int)it->n_opts - count > w->sort_groups ? (int)it->n_opts - count : w->sort_groups;
        }
        const char *stem = book_pack_str(pack, it->stem), *ex = book_pack_str(pack, it->explanation);
        const int sb = (int)strlen(stem), eb = (int)strlen(ex);
        const int sl = lines_of(sp, bold, stem, T_STEM, w->width), el = lines_of(sp, body, ex, T_TEXT, w->width);
        int ab = 0, al = 0, total = sl + el;
        for (uint32_t k = 0; k < it->n_opts; k++) {
            const mgpk_opt_t *o = book_opt(pack, it->first_opt + k);
            const char *a = book_pack_str(pack, o->text);
            const char *b = t == SP_MATCH ? book_pack_str(pack, o->aux) : "";
            const int bytes = (int)(strlen(a) + strlen(b));
            const int lines = t == SP_MATCH ? (int)mg_maxf((float)lines_of(sp, bold, a, 15.0f, w->width * 0.5f - 30.0f), (float)lines_of(sp, bold, b, 15.0f, w->width * 0.5f - 30.0f))
                                            : lines_of(sp, bold, a, t == SP_SIMPLU || t == SP_MULTIPLU || t == SP_ORDER ? T_TEXT : 15.0f, row_w);
            ab = bytes > ab ? bytes : ab;
            al = lines > al ? lines : al;
            total += lines;
        }
        w->answer_bytes[t] = ab > w->answer_bytes[t] ? ab : w->answer_bytes[t];
        w->answer_lines[t] = al > w->answer_lines[t] ? al : w->answer_lines[t];
        w->stem_bytes[t] = sb > w->stem_bytes[t] ? sb : w->stem_bytes[t];
        w->stem_lines[t] = sl > w->stem_lines[t] ? sl : w->stem_lines[t];
        w->expl_bytes[t] = eb > w->expl_bytes[t] ? eb : w->expl_bytes[t];
        w->expl_lines[t] = el > w->expl_lines[t] ? el : w->expl_lines[t];
        const int sc = count * 100000 + total;
        if (sc > score[t]) {
            score[t] = sc, w->most[t] = count, w->most_item[t] = i;
        }
        if (total > long_score[t]) {
            long_score[t] = total, w->long_item[t] = i;
        }
    }
    for (uint32_t a = 0; a < pack->atom_count; a++) {
        const char *s = book_pack_str(pack, book_atom(pack, a)->text);
        const int b = (int)strlen(s), l = lines_of(sp, body, s, T_TEXT, w->width);
        w->teach_bytes = b > w->teach_bytes ? b : w->teach_bytes;
        w->teach_lines = l > w->teach_lines ? l : w->teach_lines;
    }
    for (int f = 0; sp->figs_ok && f < sp->figs.count; f++) {
        BookFig fg;
        if (!book_figs_get(&sp->figs, f, &fg)) {
            continue;
        }
        if (fg.w * fg.h > w->fig_w * w->fig_h) {
            w->fig_w = fg.w, w->fig_h = fg.h;
        }
        int n = 0;
        for (uint32_t r = 0; r < pack->figlabel_count; r++) {
            n += strcmp(book_pack_str(pack, book_figlabel(pack, r)->fig), fg.id) == 0 ? 1 : 0;
        }
        w->fig_labels = n > w->fig_labels ? n : w->fig_labels;
    }
    static const char *const names[SP_TYPES] = {"simplu", "multiplu", "card", "cloze", "sort", "match", "order", "label"};
    char line[1024];
    size_t o = (size_t)SDL_snprintf(line, sizeof(line), "book: worst cases at %.0f pt:", (double)w->width);
    for (int t = 0; t < SP_TYPES && o < sizeof(line); t++) {
        o += (size_t)SDL_snprintf(line + o, sizeof(line) - o, " %s n=%d answer=%dB/%dL stem=%dB/%dL expl=%dB/%dL;", names[t], w->most[t], w->answer_bytes[t],
                                  w->answer_lines[t], w->stem_bytes[t], w->stem_lines[t], w->expl_bytes[t], w->expl_lines[t]);
    }
    mg_log_info("%s sort groups=%d teach=%dB/%dL figure=%dx%d labels=%d (%.1f ms)", line, w->sort_groups, w->teach_bytes, w->teach_lines, w->fig_w,
                w->fig_h, w->fig_labels,
                (double)(SDL_GetTicksNS() - t0) / 1e6);
}

bool script_player_open(ScriptPlayer *sp, const BookPack *pack, uint32_t script_index, const char *presenter_name)
{
    const mgpk_script_t *sc = pack && pack->ok ? book_script(pack, script_index) : NULL;
    if (!sc || sc->step_count == 0) {
        mg_log_error("script player: no script %u", script_index);
        return false;
    }
    sp->pack = pack;
    sp->script = script_index;
    SDL_snprintf(sp->presenter, sizeof(sp->presenter), "%s", presenter_name ? presenter_name : "");
    sp->checks = sp->checks_answered = sp->checks_right = 0;
    for (uint32_t k = 0; k < sc->step_count; k++) {
        const mgpk_step_t *st = book_step(pack, sc->first_step + k);
        sp->checks += st && st->stage == SP_CHECK ? 1 : 0;
    }
    sp->active = true;
    sp->finished = false;
    sp->step = 0;
    sp->last_w = 0.0f;
    sp->why[0] = 0;
    step_enter(sp, 0.0);
    mg_log_info("script player: %s (%u steps, %d checks) with %s", S(sp, sc->id), sc->step_count, sp->checks, sp->presenter);
    return true;
}

bool script_player_active(const ScriptPlayer *sp) { return sp->active; }
bool script_player_finished(const ScriptPlayer *sp) { return sp->finished; }

void script_player_close(ScriptPlayer *sp)
{
    if (sp->active) {
        mg_log_info("script player: closed at step %d (%d / %d checks answered)", sp->step + 1, sp->checks_answered, sp->checks);
    }
    sp->active = false;
    sp->finished = false;
    sp->drag = -1;
}

/* ------------------------------------------------------------------ answers */

static bool evaluate(const ScriptPlayer *sp)
{
    switch (sp->type) {
    case SP_SIMPLU:
    case SP_MULTIPLU: {
        uint32_t want = 0;
        for (int k = 0; k < sp->n; k++) {
            want |= opt_of(sp, k)->ok ? 1u << k : 0u;
        }
        return sp->sel == want && want != 0;
    }
    case SP_CARD:
        return sp->right;
    case SP_CLOZE:
        for (int g = 0; g < sp->n_gaps; g++) {
            if (sp->gap_fill[g] < 0 || strcmp(S(sp, book_opt(sp->pack, (uint32_t)sp->cand_opt[sp->gap_fill[g]])->text), S(sp, opt_of(sp, g)->text)) != 0) {
                return false;
            }
        }
        return true;
    case SP_SORT:
        for (int e = 0; e < sp->n; e++) {
            if (sp->place[e] != sp->entry_group[e]) {
                return false;
            }
        }
        return true;
    case SP_MATCH:
    case SP_LABEL:
        for (int i = 0; i < sp->n; i++) {
            if (sp->place[i] != i) {
                return false;
            }
        }
        return true;
    case SP_ORDER:
        for (int p = 0; p < sp->n; p++) {
            if (opt_of(sp, sp->perm[p])->group != p) {
                return false;
            }
        }
        return true;
    default:
        return false;
    }
}

static void next_step(ScriptPlayer *sp, double t);

static void answer(ScriptPlayer *sp, Ui *ui, BookProgress *progress, double now_ms, float fx_x, float fx_y)
{
    sp->right = evaluate(sp);
    sp->answered = true;
    sp->reveal = true;
    if (sp->stage == SP_CHECK) {
        sp->checks_answered++;
        if (sp->right) { /* only a check answered right teaches: its facts (multiplu: only fully right, evaluate) */
            sp->checks_right++;
            for (uint32_t k = 0; progress && k < sp->it->n_facts; k++) {
                const uint32_t f = book_ref(sp->pack, sp->it->first_fact_ref + k);
                if (f != MGPK_NONE) {
                    book_progress_set_fact(progress, (int)f, now_ms);
                }
            }
        }
    }
    mg_log_info("script player: %s item %s (type %d) answered %s", sp->stage == SP_CHECK ? "check" : "predict", S(sp, sp->it->id), sp->type,
                sp->right ? "right" : "wrong");
    ui_haptic(sp->right ? HAPTIC_CORRECT : HAPTIC_WRONG);
    if (ui && ui->fx) {
        ui_fx_tier(ui->fx, sp->right ? UI_T2 : UI_T2_FAIL, fx_x, fx_y, true);
    }
    relayout(sp);
}

/* ------------------------------------------------------------------ widgets */

typedef struct Body {
    float x, w, v0, v1; /* content column and the view's top / bottom (screen pt) */
} Body;

static bool visible(const Body *b, float y, float h) { return y + h >= b->v0 - 20.0f && y <= b->v1 + 20.0f; }

static void squash(Ui *ui, uint32_t key, float cx, float bottom)
{
    const float sx = ui->anim ? ui_anim_get(ui->anim, key, UI_SX, 1.0f) : 1.0f, sy = ui->anim ? ui_anim_get(ui->anim, key, UI_SY, 1.0f) : 1.0f;
    gfx_ui_group(ui->gfx, sx, 0.0f, 0.0f, sy, cx, bottom);
}

static UiAnswer answer_state(const ScriptPlayer *sp, int k)
{
    if (!sp->answered) {
        return UI_ANS_DEFAULT;
    }
    const bool ok = opt_of(sp, k)->ok != 0, picked = (sp->sel >> k) & 1u;
    if (picked) {
        return ok ? UI_ANS_OK : UI_ANS_BAD;
    }
    return ok ? UI_ANS_REVEAL : UI_ANS_DIM;
}

static void where(ScriptPlayer *sp, const char *id, float y, float h)
{
    if (sp->n_where < (int)(sizeof(sp->where) / sizeof(sp->where[0]))) {
        sp->where[sp->n_where].key = ui_key(id), sp->where[sp->n_where].y = y, sp->where[sp->n_where++].h = h;
    }
}

/* a tappable surface holding wrapped pack text; mark: 0 none, 1 an empty box, 2 a ticked box (multiplu) */
static float text_row(ScriptPlayer *sp, Ui *ui, const Body *b, const char *id, const char *text, float x, float y, float w, UiAnswer state, int mark,
                      bool tappable, bool selected, bool *clicked)
{
    const float lip = k_ui.lip_row, pad = 13.0f, tx = x + 16.0f + (mark ? 34.0f : 0.0f), tw = w - (tx - x) - 40.0f;
    const float face_h = mg_maxf(k_ui.row_h, text_h(sp, ui, text, TEXT_FACE_BOLD, T_TEXT, tw) + 2.0f * pad), h = face_h + lip;
    const uint32_t key = ui_key(id);
    bool held = false;
    where(sp, id, y, h);
    ui->hclass = HC_ANSWER;
    const bool hit = tappable && ui_press(ui, id, x, y, w, h, &held, false);
    ui->hclass = HC_LINK;
    if (clicked) {
        *clicked = hit;
    }
    if (!visible(b, y, h)) {
        return h;
    }
    UiColor face = C(k_ui.surface_tint), lc = C(k_ui.secondary_lip), tc = C(k_ui.ink);
    if (state == UI_ANS_OK) {
        face = C(k_ui.ok), lc = C(k_ui.ok_lip), tc = C(k_ui.white);
    } else if (state == UI_ANS_BAD) {
        face = C(k_ui.bad), lc = C(k_ui.bad_lip), tc = C(k_ui.white);
    }
    const float a = state == UI_ANS_DIM ? 0.55f : 1.0f;
    ui_depth(ui, 2, key, 0);
    squash(ui, key, x + w * 0.5f, y + h);
    ui_surface(ui, x, y, w, face_h, k_ui.r_row, lip, fade(face, a), fade(lc, a), 0.0f, C(0));
    if (state == UI_ANS_REVEAL || selected) {
        ui_shape(ui->gfx, &(UiShape){.x = x, .y = y, .w = w, .h = face_h, .radius = k_ui.r_row, .stroke = k_ui.ring_width, .color = C(selected ? k_ui.accent : k_ui.ok)});
    }
    if (mark) {
        const float bs = 24.0f, bx = x + 16.0f, by = y + face_h * 0.5f - bs * 0.5f;
        ui_shape(ui->gfx, &(UiShape){.x = bx, .y = by, .w = bs, .h = bs, .radius = 7.0f, .stroke = mark == 2 ? 0.0f : 2.0f,
                                     .color = mark == 2 ? C(k_ui.accent) : fade(tc, 0.6f * a)});
        if (mark == 2) {
            ui_icon(ui, UI_ICON_CHECK, bx + bs * 0.5f, by + bs * 0.5f, 16.0f, C(k_ui.white));
        }
    }
    text_block(sp, ui, text, TEXT_FACE_BOLD, T_TEXT, fade(tc, a), tx, y + pad, tw, b->v0, b->v1);
    if (state == UI_ANS_OK || state == UI_ANS_REVEAL) {
        ui_icon(ui, UI_ICON_CHECK, x + w - 22.0f, y + face_h * 0.5f, 20.0f, state == UI_ANS_OK ? tc : C(k_ui.ok));
    } else if (state == UI_ANS_BAD) {
        ui_icon(ui, UI_ICON_CLOSE, x + w - 22.0f, y + face_h * 0.5f, 20.0f, tc);
    }
    gfx_ui_group_off(ui->gfx);
    ui_depth_end(ui);
    return h;
}

/* a chip: a white pill-ish surface with wrapped text (cloze candidates, sort entries, labels, match sides); the height */
static float chip_h(ScriptPlayer *sp, const Ui *ui, const char *text, float w)
{
    return mg_maxf(k_ui.target_min, text_h(sp, ui, text, TEXT_FACE_BOLD, 15.0f, w - 24.0f) + 18.0f) + k_ui.lip_pill;
}

static bool chip(ScriptPlayer *sp, Ui *ui, const Body *b, const char *id, const char *text, float x, float y, float w, float h, uint32_t fill,
                 uint32_t lip, uint32_t ink, bool ring, bool tappable, float alpha)
{
    bool held = false;
    if (tappable) {
        where(sp, id, y, h);
    }
    const bool hit = tappable && ui_press(ui, id, x, y, w, h, &held, false);
    if (!visible(b, y, h)) {
        return hit;
    }
    const uint32_t key = ui_key(id);
    const float face_h = h - k_ui.lip_pill;
    ui_depth(ui, 2, key, 0);
    squash(ui, key, x + w * 0.5f, y + h);
    ui_surface(ui, x, y, w, face_h, 14.0f, k_ui.lip_pill, fade(C(fill), alpha), fade(C(lip), alpha), 0.0f, C(0));
    if (ring) {
        ui_shape(ui->gfx, &(UiShape){.x = x, .y = y, .w = w, .h = face_h, .radius = 14.0f, .stroke = k_ui.ring_width, .color = C(k_ui.accent)});
    }
    const float th = text_h(sp, ui, text, TEXT_FACE_BOLD, 15.0f, w - 24.0f);
    text_block(sp, ui, text, TEXT_FACE_BOLD, 15.0f, fade(C(ink), alpha), x + 12.0f, y + (face_h - th) * 0.5f, w - 24.0f, b->v0, b->v1);
    gfx_ui_group_off(ui->gfx);
    ui_depth_end(ui);
    return hit;
}

static void note_flags(ScriptPlayer *sp, Ui *ui, const Body *b, float *y)
{
    if (!sp->it || !(sp->it->flags & (MGPK_ITEM_NEEDS_REVIEW | MGPK_ITEM_UNVERIFIED_OCR))) {
        return;
    }
    /* the book as printed: errata and hidden text carry a marker and the review note, never a rewrite */
    const char *tag = str(sp->it->flags & MGPK_ITEM_NEEDS_REVIEW ? "sp_errata" : "sp_ocr");
    const float w = ui_tag_chip_w(ui, tag);
    if (visible(b, *y, k_ui.tag_h)) {
        ui_tag_chip(ui, tag, b->x + w, *y);
    }
    *y += k_ui.tag_h + k_ui.tag_lip + 6.0f;
    if (sp->it->review_note != MGPK_NONE && S(sp, sp->it->review_note)[0]) {
        *y += text_block(sp, ui, S(sp, sp->it->review_note), TEXT_FACE_BODY, T_NOTE, C(k_ui.ink_soft), b->x, *y, b->w, b->v0, b->v1) + GAP;
    }
}

static const char *const k_hint[SP_TYPES] = {"sp_hint_simplu", "sp_hint_multiplu", "sp_hint_card", "sp_hint_cloze",
                                              "sp_hint_sort",   "sp_hint_match",    "sp_hint_order", "sp_hint_label"};

/* sort / label / order: the chip index whose drop lands at (x, y), or -1 */
static int drop_at(const ScriptPlayer *sp, float x, float y)
{
    for (int i = 0; i < sp->n_drop; i++) {
        const float *r = sp->drop_rect[i];
        if (x >= r[0] && x <= r[0] + r[2] && y >= r[1] && y <= r[1] + r[3]) {
            return i;
        }
    }
    return -1;
}

static void add_drop(ScriptPlayer *sp, int i, float x, float y, float w, float h)
{
    if (i >= 0 && i <= SP_OPTS) {
        sp->drop_rect[i][0] = x, sp->drop_rect[i][1] = y, sp->drop_rect[i][2] = w, sp->drop_rect[i][3] = h;
        sp->n_drop = i + 1 > sp->n_drop ? i + 1 : sp->n_drop;
    }
}

static void keep_chip(ScriptPlayer *sp, int i, const char *id, float x, float y, float w, float h)
{
    sp->drag_key[i] = ui_key(id);
    sp->chip_rect[i][0] = x, sp->chip_rect[i][1] = y, sp->chip_rect[i][2] = w, sp->chip_rect[i][3] = h;
}

/* order: move the opt at position `from` to position `to` */
static void order_move(ScriptPlayer *sp, int from, int to)
{
    if (from < 0 || to < 0 || from >= sp->n || to >= sp->n || from == to) {
        return;
    }
    const uint8_t v = sp->perm[from];
    if (from < to) {
        memmove(&sp->perm[from], &sp->perm[from + 1], (size_t)(to - from));
    } else {
        memmove(&sp->perm[to + 1], &sp->perm[to], (size_t)(from - to));
    }
    sp->perm[to] = v;
}

/* sort: a colour per bin (tokens) */
static uint32_t group_colour(int g)
{
    const uint32_t c[SP_GROUPS] = {k_ui.accent, k_ui.coin, k_ui.status_menu, k_ui.bad, k_ui.ok, k_ui.primary_lip, k_ui.ink_soft, k_ui.secondary_lip};
    return c[g % SP_GROUPS];
}

/* label: the box whose centre is nearest (x, y), -1 none */
static int nearest_box(const ScriptPlayer *sp, float x, float y)
{
    int best = -1;
    float bd = 1e30f;
    for (int i = 0; i < sp->n_boxes; i++) {
        const float dx = sp->box_c[i][0] - x, dy = sp->box_c[i][1] - y, d = dx * dx + dy * dy;
        if (d < bd) {
            bd = d, best = i;
        }
    }
    return best;
}

/* the item's widget; returns the height used */
static float item_widget(ScriptPlayer *sp, Ui *ui, const Body *b, float y0)
{
    char id[24];
    float y = y0;
    const bool live = !sp->answered;
    sp->n_drop = 0;
    sp->n_where = 0;
    memset(sp->drop_rect, 0, sizeof(sp->drop_rect));
    switch (sp->type) {
    case SP_SIMPLU:
    case SP_MULTIPLU:
        for (int k = 0; k < sp->n; k++) {
            SDL_snprintf(id, sizeof(id), "sp_opt%d", k);
            bool hit = false;
            const int mark = sp->type == SP_MULTIPLU ? (((sp->sel >> k) & 1u) ? 2 : 1) : 0;
            y += text_row(sp, ui, b, id, S(sp, opt_of(sp, k)->text), b->x, y, b->w, answer_state(sp, k), sp->answered ? 0 : mark, live, false, &hit) + 8.0f;
            if (hit && sp->type == SP_SIMPLU) {
                sp->selected = 100 + k; /* answered by the frame, after the body (it has the progress) */
            } else if (hit) {
                sp->sel ^= 1u << k;
                relayout(sp);
            }
        }
        break;
    case SP_CARD: {
        /* the card: the stem's question on the front, the answer on the back; a flip turns it over in 0.3 s */
        const float cw = b->w, t = sp->flipped ? (float)((ui->anim ? ui->anim->now : 0.0) - sp->flip_t) / 0.3f : 1.0f;
        const float k = t >= 1.0f ? 1.0f : fabsf(cosf(3.14159265f * mg_maxf(t, 0.0f)));
        const bool back = sp->flipped && t >= 0.5f;
        const char *text = back ? S(sp, opt_of(sp, 0)->text) : str("sp_card_front");
        const float th = text_h(sp, ui, text, back ? TEXT_FACE_BOLD : TEXT_FACE_DISPLAY, back ? T_STEM : 22.0f, cw - 48.0f), ch = mg_maxf(120.0f, th + 48.0f);
        if (visible(b, y, ch)) {
            gfx_ui_group(ui->gfx, k, 0.0f, 0.0f, 1.0f, b->x + cw * 0.5f, y + ch * 0.5f);
            ui_surface(ui, b->x, y, cw, ch, k_ui.r_card * 0.7f, k_ui.lip_card, C(back ? k_ui.surface : k_ui.accent), C(back ? k_ui.lip_mint : k_ui.accent_lip), 0.0f, C(0));
            text_block(sp, ui, text, back ? TEXT_FACE_BOLD : TEXT_FACE_DISPLAY, back ? T_STEM : 22.0f, C(back ? k_ui.ink : k_ui.white), b->x + 24.0f,
                       y + (ch - th) * 0.5f, cw - 48.0f, b->v0, b->v1);
            gfx_ui_group_off(ui->gfx);
        }
        y += ch + k_ui.lip_card;
        break;
    }
    case SP_CLOZE: {
        /* the gaps: filled ones tap to clear */
        float x = b->x;
        for (int g = 0; g < sp->n_gaps; g++) {
            const int c = sp->gap_fill[g];
            const char *t = c >= 0 ? S(sp, book_opt(sp->pack, (uint32_t)sp->cand_opt[c])->text) : "______";
            const float w = mg_minf(mg_maxf(text_width(ui_face(ui, TEXT_FACE_BOLD), t, 15.0f) + 24.0f, 88.0f), b->w);
            const float h = chip_h(sp, ui, t, w);
            if (x + w > b->x + b->w) {
                x = b->x, y += h + 8.0f;
            }
            SDL_snprintf(id, sizeof(id), "sp_gap%d", g);
            const bool ok = sp->answered && c >= 0 && strcmp(t, S(sp, opt_of(sp, g)->text)) == 0;
            if (chip(sp, ui, b, id, t, x, y, w, h, sp->answered ? (ok ? k_ui.ok : k_ui.bad) : c >= 0 ? k_ui.primary : k_ui.surface_tint,
                     sp->answered ? (ok ? k_ui.ok_lip : k_ui.bad_lip) : c >= 0 ? k_ui.primary_lip : k_ui.secondary_lip, sp->answered ? k_ui.white : k_ui.ink, false,
                     live && c >= 0, 1.0f)) {
                sp->gap_fill[g] = -1;
                build_cloze(sp);
                relayout(sp);
            }
            x += w + 8.0f;
            if (g == sp->n_gaps - 1) {
                y += h + 14.0f;
            }
        }
        if (sp->answered) { /* the keyed answer under a wrong fill */
            break;
        }
        x = b->x;
        float row_h = 0.0f;
        for (int c = 0; c < sp->n_cands; c++) {
            const char *t = S(sp, book_opt(sp->pack, (uint32_t)sp->cand_opt[c])->text);
            const float w = mg_minf(mg_maxf(text_width(ui_face(ui, TEXT_FACE_BOLD), t, 15.0f) + 24.0f, 64.0f), b->w);
            const float h = chip_h(sp, ui, t, w);
            if (x + w > b->x + b->w) {
                x = b->x, y += row_h + 8.0f, row_h = 0.0f;
            }
            bool used = false;
            for (int g = 0; g < sp->n_gaps; g++) {
                used = used || sp->gap_fill[g] == c;
            }
            SDL_snprintf(id, sizeof(id), "sp_cand%d", c);
            if (chip(sp, ui, b, id, t, x, y, w, h, k_ui.surface, k_ui.lip_mint, k_ui.ink, false, !used, used ? 0.4f : 1.0f)) {
                for (int g = 0; g < sp->n_gaps; g++) {
                    if (sp->gap_fill[g] < 0) {
                        sp->gap_fill[g] = (int8_t)c;
                        break;
                    }
                }
                build_cloze(sp);
                relayout(sp);
            }
            x += w + 8.0f;
            row_h = mg_maxf(row_h, h);
        }
        y += row_h;
        break;
    }
    case SP_SORT: {
        /* Up to 22 entries in up to 5 groups (the real pack): every entry stays in one list, tinted with the colour of the bin it
         * went to; the bins are compact (name + count) and, on a wide card, stay in view beside the list while it scrolls. An
         * entry is dragged onto a bin, or tapped and then a bin tapped; a placed entry tapped again can go to another bin. */
        const bool side = b->w >= 520.0f;
        const float lw = side ? b->w * 0.42f : b->w;
        const float gx = side ? b->x + lw + 14.0f : b->x, gw = side ? b->w - lw - 14.0f : b->w;
        const int ng = sp->n_groups > 0 ? sp->n_groups : 1;
        int cols = (int)((gw + 10.0f) / 130.0f);
        cols = cols < 1 ? 1 : cols > ng ? ng : cols;
        const int nrows = (ng + cols - 1) / cols;
        const float bw = (gw - 10.0f * (float)(cols - 1)) / (float)cols;
        float row_y[SP_GROUPS + 1];
        row_y[0] = 0.0f;
        for (int r = 0; r < nrows; r++) {
            float bh = 0.0f;
            for (int gi = r * cols; gi < ng && gi < (r + 1) * cols; gi++) {
                bh = mg_maxf(bh, 10.0f + text_h(sp, ui, S(sp, sp->group_text[gi]), TEXT_FACE_DISPLAY, 15.0f, bw - 20.0f) + 30.0f);
            }
            row_y[r + 1] = row_y[r] + mg_maxf(bh, k_ui.target_min + 12.0f) + 10.0f;
        }
        const float bins_h = row_y[nrows] - 10.0f;
        /* the list */
        const float list_y = side ? y : y + bins_h + 12.0f;
        float py = list_y;
        for (int p = 0; p < sp->n; p++) {
            const int e = sp->perm[p];
            const int g = sp->place[e];
            const char *t = S(sp, opt_of(sp, e)->text);
            const float h = chip_h(sp, ui, t, lw);
            const bool ok = g == sp->entry_group[e];
            SDL_snprintf(id, sizeof(id), "sp_ent%d", e);
            keep_chip(sp, e, id, b->x, py, lw, h);
            const uint32_t fill = sp->answered ? (ok ? k_ui.ok : k_ui.bad) : g >= 0 ? k_ui.surface_tint : k_ui.surface;
            if (chip(sp, ui, b, id, t, b->x, py, lw, h, fill, sp->answered ? (ok ? k_ui.ok_lip : k_ui.bad_lip) : k_ui.lip_mint,
                     sp->answered ? k_ui.white : k_ui.ink, sp->selected == e, live, sp->drag == e ? 0.3f : 1.0f)) {
                sp->selected = sp->selected == e ? -1 : e;
            }
            if (g >= 0 && visible(b, py, h)) { /* the bin it is in: a stripe of the bin's colour down its left edge */
                ui_shape(ui->gfx, &(UiShape){.x = b->x + 3.0f, .y = py + 6.0f, .w = 6.0f, .h = h - k_ui.lip_pill - 12.0f, .radius = 3.0f,
                                             .color = C(group_colour(g))});
            }
            py += h + 8.0f;
        }
        add_drop(sp, SP_OPTS, b->x, list_y, lw, mg_maxf(py - list_y, 44.0f)); /* back off every bin */
        /* the bins: beside the list, held in view while it scrolls (never past the list's ends) */
        float by0 = y;
        if (side) {
            by0 = mg_maxf(y, b->v0 + 4.0f);
            by0 = mg_minf(by0, mg_maxf(y, py - 8.0f - bins_h));
        }
        for (int gi = 0; gi < ng; gi++) {
            const int r = gi / cols;
            const float bx = gx + (bw + 10.0f) * (float)(gi % cols), gy = by0 + row_y[r], bh = row_y[r + 1] - row_y[r] - 10.0f;
            int count = 0;
            for (int e = 0; e < sp->n; e++) {
                count += sp->place[e] == gi ? 1 : 0;
            }
            if (visible(b, gy, bh)) {
                ui_shape(ui->gfx, &(UiShape){.x = bx, .y = gy, .w = bw, .h = bh, .radius = k_ui.r_row, .color = C(k_ui.surface_tint)});
                ui_shape(ui->gfx, &(UiShape){.x = bx, .y = gy, .w = bw, .h = bh, .radius = k_ui.r_row, .stroke = sp->selected >= 0 || sp->drag >= 0 ? 3.0f : 2.0f,
                                             .color = C(group_colour(gi))});
                text_block(sp, ui, S(sp, sp->group_text[gi]), TEXT_FACE_DISPLAY, 15.0f, C(k_ui.ink), bx + 10.0f, gy + 10.0f, bw - 20.0f, b->v0, b->v1);
                char num[16];
                SDL_snprintf(num, sizeof(num), "%d", count);
                ui_shape(ui->gfx, &(UiShape){.x = bx + 10.0f, .y = gy + bh - 26.0f, .w = 30.0f, .h = 20.0f, .radius = -1.0f, .color = C(group_colour(gi))});
                ui_text_c(ui, TEXT_FACE_BOLD, bx + 25.0f, gy + bh - 16.0f, 12.0f, C(k_ui.white), num);
            }
            add_drop(sp, gi, bx, gy, bw, bh);
            bool held = false;
            SDL_snprintf(id, sizeof(id), "sp_bin%d", gi);
            where(sp, id, gy, bh);
            if (live && ui_press(ui, id, bx, gy, bw, bh, &held, true) && sp->selected >= 0) {
                sp->place[sp->selected] = (int8_t)gi;
                sp->selected = -1;
                relayout(sp);
            }
        }
        y = side ? mg_maxf(py, y + bins_h) : py;
        break;
    }
    case SP_MATCH: {
        /* left fixed, right shuffled; tap a left, then a right: a pair (its colour on both) */
        const float cw = (b->w - 12.0f) * 0.5f;
        static const int pal = 4;
        for (int r = 0; r < sp->n; r++) {
            const int li = r, ri = sp->perm[r];
            const char *lt = S(sp, opt_of(sp, li)->text), *rt = S(sp, opt_of(sp, ri)->aux);
            const float h = mg_maxf(chip_h(sp, ui, lt, cw), chip_h(sp, ui, rt, cw));
            int rl = -1; /* the left paired with this right */
            for (int i = 0; i < sp->n; i++) {
                rl = sp->place[i] == ri ? i : rl;
            }
            const uint32_t lc = sp->place[li] >= 0 ? k_ui.confetti[li % pal] : k_ui.surface;
            const uint32_t rc = rl >= 0 ? k_ui.confetti[rl % pal] : k_ui.surface;
            const bool lok = sp->place[li] == li, rok = rl == ri;
            SDL_snprintf(id, sizeof(id), "sp_ml%d", li);
            if (chip(sp, ui, b, id, lt, b->x, y, cw, h, sp->answered ? (lok ? k_ui.ok : k_ui.bad) : lc, sp->answered ? (lok ? k_ui.ok_lip : k_ui.bad_lip) : k_ui.lip_mint,
                     sp->answered ? k_ui.white : k_ui.ink, sp->selected == li, live, 1.0f)) {
                sp->selected = sp->selected == li ? -1 : li;
            }
            SDL_snprintf(id, sizeof(id), "sp_mr%d", ri);
            if (chip(sp, ui, b, id, rt, b->x + cw + 12.0f, y, cw, h, sp->answered ? (rok ? k_ui.ok : k_ui.bad) : rc,
                     sp->answered ? (rok ? k_ui.ok_lip : k_ui.bad_lip) : k_ui.lip_mint, sp->answered ? k_ui.white : k_ui.ink, false, live, 1.0f) &&
                sp->selected >= 0) {
                for (int i = 0; i < sp->n; i++) { /* a right belongs to one left */
                    sp->place[i] = sp->place[i] == ri ? -1 : sp->place[i];
                }
                sp->place[sp->selected] = (int8_t)ri;
                sp->selected = -1;
            }
            y += h + 8.0f;
        }
        break;
    }
    case SP_ORDER: {
        /* rows in their current order; drag one to a new place (or tap one, then the place) */
        for (int p = 0; p < sp->n; p++) {
            const int o = sp->perm[p];
            const char *t = S(sp, opt_of(sp, o)->text);
            const bool ok = opt_of(sp, o)->group == p;
            SDL_snprintf(id, sizeof(id), "sp_ord%d", opt_of(sp, o)->group);
            bool hit = false;
            const float h0 = y;
            UiAnswer st = sp->answered ? (ok ? UI_ANS_OK : UI_ANS_BAD) : UI_ANS_DEFAULT;
            const float h = text_row(sp, ui, b, id, t, b->x, y, b->w, st, 0, live, sp->selected == p, &hit);
            keep_chip(sp, p, id, b->x, y, b->w, h);
            add_drop(sp, p, b->x, h0, b->w, h + 8.0f);
            if (hit) {
                if (sp->selected >= 0 && sp->selected != p) {
                    order_move(sp, sp->selected, p);
                    sp->selected = -1;
                    relayout(sp);
                } else {
                    sp->selected = sp->selected == p ? -1 : p;
                }
            }
            if (visible(b, y, h)) { /* the position number */
                char num[8];
                SDL_snprintf(num, sizeof(num), "%d", p + 1);
                ui_text(ui, TEXT_FACE_DISPLAY, b->x + b->w - 30.0f, y + 6.0f, 13.0f, C(st == UI_ANS_DEFAULT ? k_ui.teal_text : k_ui.white), num);
            }
            y += h + 8.0f;
        }
        break;
    }
    case SP_LABEL: {
        /* the figure with blank boxes; the labels beside it (or under it on a narrow card) */
        const bool side = b->w >= 520.0f;
        const float fw = side ? b->w * 0.62f : b->w, fh = mg_maxf((b->v1 - b->v0) - 24.0f, 140.0f);
        float fr[4];
        fig_draw(sp, ui, b->x + 4.0f, y + 4.0f, fw - 8.0f, fh, fr);
        const float sx = fr[2] / sp->crop_w, sy = fr[3] / sp->crop_h;
        for (int i = 0; i < sp->n_boxes; i++) {
            const float bx = fr[0] + sp->box[i][0] * sx, by = fr[1] + sp->box[i][1] * sy;
            const float bw = mg_maxf((sp->box[i][2] - sp->box[i][0]) * sx, 18.0f), bh = mg_maxf((sp->box[i][3] - sp->box[i][1]) * sy, 14.0f);
            int lab = -1;
            for (int k = 0; k < sp->n; k++) {
                lab = sp->place[k] == i ? k : lab;
            }
            const bool ok = lab == i;
            const uint32_t fill = sp->answered ? (ok ? k_ui.ok : k_ui.bad) : lab >= 0 ? k_ui.primary : k_ui.surface;
            if (visible(b, by, bh)) {
                ui_shape(ui->gfx, &(UiShape){.x = bx, .y = by, .w = bw, .h = bh, .radius = 5.0f, .stroke = -2.0f, .color = C(fill),
                                             .color2 = C(sp->selected >= 0 ? k_ui.accent : k_ui.ink)});
                { /* every box carries its number (its button below has it too); a placed label shows in the box if it still reads
                   * at 9 pt, else the number stays: text never spills over a neighbouring box */
                    const char *t = lab >= 0 ? S(sp, opt_of(sp, lab)->text) : "";
                    float ts = mg_minf(13.0f, bh * 0.8f);
                    const float tw = text_width(ui_face(ui, TEXT_FACE_BOLD), t, ts);
                    ts = tw > bw - 4.0f && tw > 0.0f ? ts * (bw - 4.0f) / tw : ts;
                    char num[8];
                    SDL_snprintf(num, sizeof(num), "%d", i + 1);
                    const bool text = lab >= 0 && ts >= 9.0f;
                    ui_text_c(ui, TEXT_FACE_BOLD, bx + bw * 0.5f, by + bh * 0.5f, text ? ts : mg_minf(11.0f, bh * 0.9f),
                              C(sp->answered ? k_ui.white : lab >= 0 ? k_ui.ink : k_ui.teal_text), text ? t : num);
                }
            }
            /* boxes can be small and close (a 12-label figure): each is remembered at its own rect, and one target over the
             * whole figure takes the box nearest the finger, so neighbouring boxes never steal each other's taps */
            sp->box_c[i][0] = bx + bw * 0.5f, sp->box_c[i][1] = by + bh * 0.5f;
        }
        add_drop(sp, 0, fr[0], fr[1], fr[2], fr[3]); /* a drop anywhere on the figure: the nearest box (drag_draw_drop) */
        {
            const uint32_t fkey = ui_key("sp_fig");
            if (ui->active_id == fkey && ui->active_touch >= 0) {
                sp->fig_tap[0] = ui->in->touches[ui->active_touch].x, sp->fig_tap[1] = ui->in->touches[ui->active_touch].y;
            }
            bool held = false;
            const float tw = mg_maxf(fr[2], k_ui.target_min), th = mg_maxf(fr[3], k_ui.target_min);
            if (live && ui_press(ui, "sp_fig", fr[0], fr[1], tw, th, &held, true)) {
                const int i = nearest_box(sp, sp->fig_tap[0], sp->fig_tap[1]);
                int lab = -1;
                for (int k = 0; k < sp->n; k++) {
                    lab = sp->place[k] == i ? k : lab;
                }
                if (i >= 0 && sp->selected >= 0) {
                    for (int k = 0; k < sp->n; k++) {
                        sp->place[k] = sp->place[k] == i ? -1 : sp->place[k];
                    }
                    sp->place[sp->selected] = (int8_t)i;
                    sp->selected = -1;
                } else if (lab >= 0) {
                    sp->place[lab] = -1; /* a tap on a filled box takes its label back */
                }
            }
        }
        /* the labels: chips as wide as their text, flowing in the column */
        const float lx = side ? mg_minf(fr[0] + fr[2] + 18.0f, b->x + b->w - 200.0f) : b->x, lw = side ? b->x + b->w - lx : b->w;
        float ly = side ? y : y + fr[3] + 18.0f, cx = lx, row_h = 0.0f;
        for (int p = 0; p < sp->n; p++) {
            const int k = sp->perm[p];
            const char *t = S(sp, opt_of(sp, k)->text);
            const float cw = mg_minf(mg_maxf(text_width(ui_face(ui, TEXT_FACE_BOLD), t, 15.0f) + 24.0f, 64.0f), lw);
            const float h = chip_h(sp, ui, t, cw);
            if (cx > lx && cx + cw > lx + lw) {
                cx = lx, ly += row_h + 8.0f, row_h = 0.0f;
            }
            const bool placed = sp->place[k] >= 0;
            SDL_snprintf(id, sizeof(id), "sp_lab%d", k);
            keep_chip(sp, k, id, cx, ly, cw, h);
            if (chip(sp, ui, b, id, t, cx, ly, cw, h, placed ? k_ui.surface_tint : k_ui.surface, k_ui.lip_mint, k_ui.ink, sp->selected == k, live,
                     sp->drag == k ? 0.3f : placed ? 0.5f : 1.0f)) {
                sp->selected = sp->selected == k ? -1 : k;
            }
            if (placed && visible(b, ly, h)) { /* the number of the box it sits in, over the chip's corner */
                char num[8];
                SDL_snprintf(num, sizeof(num), "%d", sp->place[k] + 1);
                ui_shape(ui->gfx, &(UiShape){.x = cx + cw - 12.0f, .y = ly - 6.0f, .w = 18.0f, .h = 18.0f, .radius = -1.0f, .color = C(k_ui.accent)});
                ui_text_c(ui, TEXT_FACE_BOLD, cx + cw - 3.0f, ly + 3.0f, 11.0f, C(k_ui.white), num);
            }
            cx += cw + 8.0f;
            row_h = mg_maxf(row_h, h);
        }
        ly += row_h + 12.0f;
        /* the boxes as numbered 44 pt buttons: the way to place a label when the boxes on the figure are too small or too close
         * to tap (a held label goes to the button's box; a filled box's button takes its label back) */
        const float kb = k_ui.target_min + 2.0f;
        float kx = lx;
        for (int i = 0; i < sp->n_boxes; i++) {
            if (kx > lx && kx + kb > lx + lw) {
                kx = lx, ly += kb + 8.0f;
            }
            int lab = -1;
            for (int k = 0; k < sp->n; k++) {
                lab = sp->place[k] == i ? k : lab;
            }
            const bool ok = lab == i;
            char num[8];
            SDL_snprintf(num, sizeof(num), "%d", i + 1);
            SDL_snprintf(id, sizeof(id), "sp_box%d", i);
            if (chip(sp, ui, b, id, num, kx, ly, kb, kb, sp->answered ? (ok ? k_ui.ok : k_ui.bad) : lab >= 0 ? k_ui.primary : k_ui.surface,
                     sp->answered ? (ok ? k_ui.ok_lip : k_ui.bad_lip) : lab >= 0 ? k_ui.primary_lip : k_ui.lip_mint, sp->answered ? k_ui.white : k_ui.ink,
                     sp->selected >= 0, live, 1.0f)) {
                if (sp->selected >= 0) {
                    for (int k = 0; k < sp->n; k++) {
                        sp->place[k] = sp->place[k] == i ? -1 : sp->place[k];
                    }
                    sp->place[sp->selected] = (int8_t)i;
                    sp->selected = -1;
                } else if (lab >= 0) {
                    sp->place[lab] = -1;
                }
                relayout(sp);
            }
            add_drop(sp, i + 1, kx, ly, kb, kb); /* a drop on a button: its box */
            kx += kb + 8.0f;
        }
        ly += kb;
        y = mg_maxf(side ? y + fr[3] + 12.0f : y, ly);
        break;
    }
    default:
        break;
    }
    return y - y0;
}

static bool item_complete(const ScriptPlayer *sp)
{
    switch (sp->type) {
    case SP_MULTIPLU:
        return sp->sel != 0;
    case SP_CLOZE:
        for (int g = 0; g < sp->n_gaps; g++) {
            if (sp->gap_fill[g] < 0) {
                return false;
            }
        }
        return true;
    case SP_SORT:
    case SP_LABEL:
        for (int i = 0; i < sp->n; i++) {
            if (sp->place[i] < 0) {
                return false;
            }
        }
        return true;
    case SP_MATCH:
        for (int i = 0; i < sp->n; i++) {
            if (sp->place[i] < 0) {
                return false;
            }
        }
        return true;
    default:
        return true;
    }
}

/* after an answer: right / not quite, the explanation, each false option's why (multiplu) */
static float feedback(ScriptPlayer *sp, Ui *ui, const Body *b, float y0)
{
    float y = y0;
    const char *head = sp->stage == SP_PREDICT ? str(sp->right ? "sp_predict_right" : "sp_predict_wrong") : str(sp->right ? "sp_right" : "sp_wrong");
    y += text_block(sp, ui, head, TEXT_FACE_DISPLAY, 20.0f, C(sp->right ? k_ui.ok : sp->stage == SP_PREDICT ? k_ui.teal_text : k_ui.bad), b->x, y, b->w, b->v0, b->v1) + 4.0f;
    if (sp->type == SP_CLOZE && !sp->right) { /* the keyed answers */
        for (int g = 0; g < sp->n_gaps; g++) {
            y += text_block(sp, ui, S(sp, opt_of(sp, g)->text), TEXT_FACE_BOLD, T_TEXT, C(k_ui.ok), b->x, y, b->w, b->v0, b->v1);
        }
        y += 4.0f;
    }
    if (sp->it->explanation != MGPK_NONE && S(sp, sp->it->explanation)[0]) {
        y += text_block(sp, ui, S(sp, sp->it->explanation), TEXT_FACE_BODY, T_TEXT, C(k_ui.ink), b->x, y, b->w, b->v0, b->v1) + GAP;
    }
    if (sp->type == SP_MULTIPLU) {
        bool any = false;
        for (int k = 0; k < sp->n; k++) {
            const mgpk_opt_t *o = opt_of(sp, k);
            if (o->ok || o->why == MGPK_NONE || !S(sp, o->why)[0]) {
                continue;
            }
            if (!any) {
                any = true;
                y += text_block(sp, ui, str("sp_why_not"), TEXT_FACE_DISPLAY, 16.0f, C(k_ui.teal_text), b->x, y, b->w, b->v0, b->v1) + 2.0f;
            }
            y += text_block(sp, ui, S(sp, o->text), TEXT_FACE_BOLD, T_NOTE + 1.0f, C(k_ui.bad), b->x + 10.0f, y, b->w - 10.0f, b->v0, b->v1);
            y += text_block(sp, ui, S(sp, o->why), TEXT_FACE_BODY, T_NOTE + 1.0f, C(k_ui.ink_soft), b->x + 10.0f, y, b->w - 10.0f, b->v0, b->v1) + 8.0f;
        }
    }
    return y - y0;
}

static float teach(ScriptPlayer *sp, Ui *ui, const Body *b, float y0)
{
    const mgpk_step_t *st = step_of(sp, sp->step);
    const bool fig = sp->fig_shown >= 0, side = fig && b->w >= 520.0f;
    const float tw = side ? b->w * 0.56f : b->w;
    float y = y0, fy = y0;
    if (side) {
        float fr[4];
        fig_draw(sp, ui, b->x + tw + 20.0f, y0 + 4.0f, b->w - tw - 24.0f, mg_maxf(b->v1 - b->v0 - 24.0f, 120.0f), fr);
        fy = y0 + fr[3] + 16.0f;
    }
    int page = -1;
    for (uint32_t k = 0; st && k < st->n_atoms; k++) {
        const mgpk_atom_t *a = book_atom(sp->pack, book_ref(sp->pack, st->first_atom_ref + k));
        if (!a) {
            continue;
        }
        if (a->page != page) { /* the printed page, once per page */
            char pg[32];
            SDL_snprintf(pg, sizeof(pg), "%s %u", str("sp_page"), (unsigned)a->page);
            if (visible(b, y, 20.0f)) {
                ui_text(ui, TEXT_FACE_BOLD, b->x, y, 13.0f, C(k_ui.teal_text), pg);
            }
            y += 20.0f;
            page = a->page;
        }
        y += text_block(sp, ui, S(sp, a->text), TEXT_FACE_BODY, T_TEXT, C(k_ui.ink), b->x, y, tw, b->v0, b->v1) + GAP;
    }
    if (fig && !side) {
        float fr[4];
        fig_draw(sp, ui, b->x + 4.0f, y + 4.0f, b->w - 8.0f, mg_maxf((b->v1 - b->v0) * 0.9f, 120.0f), fr);
        y += fr[3] + 16.0f;
    }
    return mg_maxf(y, fy) - y0;
}

/* ------------------------------------------------------------------ drag */

static void drag_check(ScriptPlayer *sp, Ui *ui)
{
    if (sp->drag >= 0 || sp->answered || (sp->type != SP_SORT && sp->type != SP_LABEL && sp->type != SP_ORDER) || !ui->active_id ||
        ui->active_touch < 0) {
        return;
    }
    MgTouch *t = &ui->in->touches[ui->active_touch];
    if (!t->active || fabsf(t->x - t->x0) + fabsf(t->y - t->y0) < DRAG_START) {
        return;
    }
    for (int i = 0; i < SP_OPTS; i++) {
        if (sp->drag_key[i] != ui->active_id) {
            continue;
        }
        /* a drag: the touch leaves the button (no tap) and the scroll view (it skips its own owner) */
        sp->drag = i, sp->drag_touch = ui->active_touch;
        sp->grab_x = t->x0 - sp->chip_rect[i][0], sp->grab_y = t->y0 - sp->chip_rect[i][1];
        sp->drag_w = sp->chip_rect[i][2], sp->drag_h = sp->chip_rect[i][3];
        t->owner = UI_SCROLL_OWNER;
        if (ui->anim) {
            ui_anim_to(ui->anim, ui->active_id, UI_SX, 1.0f, 1.0f, k_ui.press_s, UI_EASE_OUT, NULL, false);
            ui_anim_to(ui->anim, ui->active_id, UI_SY, 1.0f, 1.0f, k_ui.press_s, UI_EASE_OUT, NULL, false);
        }
        ui->active_id = 0, ui->active_touch = -1;
        sp->selected = -1;
        ui_haptic(HAPTIC_ANSWER_SELECT);
        return;
    }
}

static void drag_draw_drop(ScriptPlayer *sp, Ui *ui, const Body *full)
{
    if (sp->drag < 0) {
        return;
    }
    const MgTouch *t = &ui->in->touches[sp->drag_touch];
    const float x = t->x - sp->grab_x, y = t->y - sp->grab_y;
    const char *text = sp->type == SP_ORDER ? S(sp, opt_of(sp, sp->perm[sp->drag])->text) : S(sp, opt_of(sp, sp->drag)->text);
    ui_depth(ui, 4, 0, 0);
    chip(sp, ui, full, "sp_dragged", text, x, y, sp->drag_w, sp->drag_h, k_ui.primary, k_ui.primary_lip, k_ui.ink, false, false, 1.0f);
    ui_depth_end(ui);
    if (t->active) {
        return;
    }
    int at = drop_at(sp, t->x, t->y); /* the finger's point, on this frame's targets */
    if (sp->type == SP_ORDER) {
        order_move(sp, sp->drag, at);
    } else if (sp->type == SP_SORT && at >= 0) {
        sp->place[sp->drag] = (int8_t)(at == SP_OPTS ? -1 : at);
    } else if (sp->type == SP_LABEL && at >= 0 && (at = at == 0 ? nearest_box(sp, t->x, t->y) : at - 1) >= 0 && at < sp->n_boxes) {
        for (int k = 0; k < sp->n; k++) {
            sp->place[k] = sp->place[k] == at ? -1 : sp->place[k];
        }
        sp->place[sp->drag] = (int8_t)at;
    }
    mg_log_info("script player: drag %d dropped on %d", sp->drag, at);
    sp->drag = -1;
    relayout(sp);
}

/* ------------------------------------------------------------------ the card */

static void next_step(ScriptPlayer *sp, double t)
{
    const mgpk_script_t *sc = book_script(sp->pack, sp->script);
    sp->step++;
    if (!sc || sp->step >= (int)sc->step_count) {
        sp->finished = sp->checks_answered >= sp->checks;
        sp->active = false;
        mg_log_info("script player: %s done, %d / %d checks right", S(sp, sc ? sc->id : 0), sp->checks_right, sp->checks);
        return;
    }
    step_enter(sp, t);
}

bool script_player_answer_key(ScriptPlayer *sp, BookProgress *progress, double now_ms, bool wrong)
{
    if (!sp->active || !sp->pack) {
        return false;
    }
    const double t = sp->step_t;
    if (sp->stage == SP_TEACH || !sp->it) {
        next_step(sp, t);
        return sp->active;
    }
    if (!wrong) { /* the widgets' state set to the key, then the same grading a tap on Check / Confirm runs */
        switch (sp->type) {
        case SP_SIMPLU:
        case SP_MULTIPLU:
            sp->sel = 0;
            for (int k = 0; k < sp->n; k++) {
                sp->sel |= opt_of(sp, k)->ok ? 1u << k : 0u;
            }
            break;
        case SP_CARD:
            sp->right = true;
            break;
        case SP_CLOZE:
            for (int g = 0; g < sp->n_gaps; g++) {
                sp->gap_fill[g] = -1;
                for (int c = 0; c < sp->n_cands; c++) {
                    if (strcmp(S(sp, book_opt(sp->pack, (uint32_t)sp->cand_opt[c])->text), S(sp, opt_of(sp, g)->text)) == 0) {
                        sp->gap_fill[g] = (int8_t)c;
                        break;
                    }
                }
            }
            break;
        case SP_SORT:
            for (int e = 0; e < sp->n; e++) {
                sp->place[e] = (int8_t)sp->entry_group[e];
            }
            break;
        case SP_MATCH:
        case SP_LABEL:
            for (int i = 0; i < sp->n; i++) {
                sp->place[i] = (int8_t)i;
            }
            break;
        case SP_ORDER:
            for (int p = 0; p < sp->n; p++) {
                for (int k = 0; k < sp->n; k++) {
                    if (opt_of(sp, k)->group == p) {
                        sp->perm[p] = (uint8_t)k;
                    }
                }
            }
            break;
        default:
            break;
        }
    } else { /* nothing chosen / nothing placed: graded wrong */
        sp->sel = 0, sp->right = false;
        memset(sp->place, -1, sizeof(sp->place));
        memset(sp->gap_fill, -1, sizeof(sp->gap_fill));
    }
    answer(sp, NULL, progress, now_ms, 0.0f, 0.0f);
    next_step(sp, t);
    return sp->active;
}

/* autoplay: wrong on an item holding skip_fact */
static bool holds_fact(const ScriptPlayer *sp, int fact)
{
    for (uint32_t k = 0; fact >= 0 && sp->it && k < sp->it->n_facts; k++) {
        if ((int)book_ref(sp->pack, sp->it->first_fact_ref + k) == fact) {
            return true;
        }
    }
    return false;
}

int script_player_frame(ScriptPlayer *sp, Ui *ui, MgApp *app, BookProgress *progress, double now_ms)
{
    if (!sp->active || !sp->pack) {
        return 0;
    }
    if (sp->autoplay && sp->step_t > 0.0 && app->time - sp->step_t > 0.3) { /* autotests: the card has shown; answer by key */
        while (script_player_answer_key(sp, progress, now_ms, sp->stage == SP_CHECK && holds_fact(sp, sp->skip_fact))) {
        }
        sp->skip_fact = -1;
        return 1;
    }
    PROF_SCOPE("book.card");
    const uint32_t allocs0 = prof_allocs_this_frame();
    sp->allocs = 0;
    GfxContext *g = app->gfx;
    const double t = app->time;
    if (sp->step_t == 0.0) {
        sp->step_t = t;
    }
    /* the card: the safe rect less a margin, centred in it (a phone's safe rect is not centred on the screen) */
    const float m = 10.0f, sl = app->safe_left, st = app->safe_top, sr = app->width_pt - app->safe_right, sb = app->height_pt - app->safe_bottom;
    const float cw = mg_minf(sr - sl - 2.0f * m, 900.0f), ch = mg_minf(sb - st - 2.0f * m - k_ui.lip_card, 640.0f);
    const float cx = sl + (sr - sl - cw) * 0.5f, cy = st + (sb - st - ch - k_ui.lip_card) * 0.5f;
    if (fabsf(cw - sp->last_w) > 0.5f) {
        relayout(sp);
        sp->last_w = cw;
    }
    sp->card[0] = cx, sp->card[1] = cy, sp->card[2] = cw, sp->card[3] = ch;
    drag_check(sp, ui);
    gfx_ui_instances(g, &sp->inst_from);
    sp->seen_from = ui->seen_count;
    sp->fig_drawn = sp->fig_placeholder = false;
    const mgpk_script_t *sc = book_script(sp->pack, sp->script);
    ui_modal_rect(ui, "sp_card", cx, cy, cw, ch);
    bool closed = false;
    /* header: presenter, stage, step counter, close */
    {
        const float hx = cx + PAD, hy = cy + 14.0f;
        const float name_w = ui_text(ui, TEXT_FACE_BOLD, hx, hy + 2.0f, T_NOTE + 1.0f, C(k_ui.teal_text), sp->presenter);
        static const char *const stage_key[3] = {"sp_stage_predict", "sp_stage_teach", "sp_stage_check"};
        const char *stage = str(stage_key[sp->stage < 3 ? sp->stage : 1]);
        ui_tag_chip(ui, stage, hx + (name_w > 0.0f ? name_w + 12.0f : 0.0f) + ui_tag_chip_w(ui, stage), hy);
        char counter[24];
        SDL_snprintf(counter, sizeof(counter), "%d / %u", sp->step + 1, sc ? sc->step_count : 0u);
        const float bs = k_ui.target_min + 2.0f, bx = cx + cw - PAD - bs, by = cy + 10.0f;
        const float tw = ui_text_w(ui, TEXT_FACE_DISPLAY, counter, 16.0f);
        ui_text(ui, TEXT_FACE_DISPLAY, bx - 14.0f - tw, hy, 16.0f, C(k_ui.ink_soft), counter);
        ui_progress(ui, "sp_prog", hx, cy + HEAD_H - 14.0f, cw - 2.0f * PAD - bs - 14.0f, 6.0f,
                    sc && sc->step_count ? (float)(sp->step + (sp->answered || sp->stage == SP_TEACH ? 1 : 0)) / (float)sc->step_count : 0.0f);
        closed = ui_icon_button(ui, "sp_close", UI_ICON_CLOSE, bx, by, bs, UI_V_SECONDARY);
    }
    /* footer: what moves the step on */
    const float fh = k_ui.button_h, fy = cy + ch - 14.0f - fh;
    /* body: everything of the step, scrolled */
    Body b = {cx + PAD, cw - 2.0f * PAD, cy + HEAD_H + 4.0f, fy - 10.0f};
    const float bh = b.v1 - b.v0;
    sp->body[0] = b.x, sp->body[1] = b.v0, sp->body[2] = b.w, sp->body[3] = bh;
    const float off = ui_scroll_begin(ui, &sp->scroll, b.x - 6.0f, b.v0, b.w + 12.0f, bh, false);
    float y = b.v0 - off;
    const float top = y;
    if (sp->step == 0 && sc) { /* the script's title on its first step */
        y += text_block(sp, ui, S(sp, sc->title), TEXT_FACE_DISPLAY, 19.0f, C(k_ui.ink), b.x, y, b.w, b.v0, b.v1) + 6.0f;
    }
    int simplu_pick = -1;
    float fb_at = -1.0f;
    if (sp->stage == SP_TEACH || !sp->it) {
        y += teach(sp, ui, &b, y);
    } else {
        note_flags(sp, ui, &b, &y);
        const char *stem = sp->type == SP_CLOZE ? sp->cloze_buf : S(sp, sp->it->stem);
        y += text_block(sp, ui, stem, TEXT_FACE_BOLD, T_STEM, C(k_ui.ink), b.x, y, b.w, b.v0, b.v1) + 6.0f;
        if (!sp->answered && sp->type >= 0 && sp->type < SP_TYPES) {
            y += text_block(sp, ui, str(k_hint[sp->type]), TEXT_FACE_BODY, T_NOTE, C(k_ui.teal_text), b.x, y, b.w, b.v0, b.v1) + 8.0f;
        }
        y += item_widget(sp, ui, &b, y) + 12.0f;
        if (sp->selected >= 100) { /* simplu: the tap is the answer */
            simplu_pick = sp->selected - 100;
            sp->selected = -1;
        }
        if (sp->answered) {
            fb_at = y - top;
            y += feedback(sp, ui, &b, y);
        }
    }
    ui_scroll_end(ui, &sp->scroll, bh, y - top + 8.0f);
    if (sp->reveal && fb_at >= 0.0f) { /* just answered: bring the verdict and the explanation into view */
        sp->reveal = false;
        sp->scroll.offset = mg_clampf(fb_at - bh * 0.4f, sp->scroll.offset, mg_maxf(sp->scroll.content - bh, 0.0f));
    }
    ui_remember(ui, "sp_body", b.x, b.v0, b.w, bh);
    if (simplu_pick >= 0) {
        sp->sel = 1u << simplu_pick;
        answer(sp, ui, progress, now_ms, b.x + b.w * 0.5f, b.v0 + bh * 0.5f);
    }
    /* footer buttons, right-aligned */
    {
        const float bw = mg_minf(220.0f, (cw - 2.0f * PAD - 12.0f) * 0.5f), bx = cx + cw - PAD - bw;
        const bool last = sc && sp->step + 1 >= (int)sc->step_count;
        char label[64];
        if (sp->stage == SP_TEACH || !sp->it || sp->answered) {
            SDL_snprintf(label, sizeof(label), "%s##sp_next", str(last ? "sp_done" : "sp_continue"));
            if (ui_button_v(ui, label, bx, fy, bw, fh, UI_V_PRIMARY, UI_F_MAIN)) {
                next_step(sp, t);
            }
        } else if (sp->type == SP_CARD) {
            if (!sp->flipped) {
                SDL_snprintf(label, sizeof(label), "%s##sp_flip", str("sp_flip"));
                if (ui_button_v(ui, label, bx, fy, bw, fh, UI_V_ACCENT, 0)) {
                    sp->flipped = true;
                    sp->flip_t = ui->anim ? ui->anim->now : t;
                    relayout(sp);
                }
            } else {
                SDL_snprintf(label, sizeof(label), "%s##sp_knew", str("sp_knew"));
                const bool knew = ui_button_v(ui, label, bx, fy, bw, fh, UI_V_OK, 0);
                SDL_snprintf(label, sizeof(label), "%s##sp_didnt", str("sp_didnt"));
                const bool didnt = ui_button_v(ui, label, bx - 12.0f - bw, fy, bw, fh, UI_V_SECONDARY, 0);
                if (knew || didnt) { /* the card is the player's own call */
                    sp->right = knew;
                    answer(sp, ui, progress, now_ms, bx + bw * 0.5f, fy);
                }
            }
        } else if (sp->type != SP_SIMPLU) {
            const bool ready = item_complete(sp);
            SDL_snprintf(label, sizeof(label), "%s##sp_confirm", str(sp->type == SP_MULTIPLU ? "sp_confirm" : "sp_check"));
            if (ui_button_v(ui, label, bx, fy, bw, fh, ready ? UI_V_PRIMARY : UI_V_DISABLED, UI_F_MAIN) && ready) {
                answer(sp, ui, progress, now_ms, bx + bw * 0.5f, fy);
            }
        }
    }
    ui_modal_end(ui);
    const Body full = {0.0f, app->width_pt, 0.0f, app->height_pt};
    drag_draw_drop(sp, ui, &full);
    sp->seen_to = ui->seen_count;
    gfx_ui_instances(g, &sp->inst_to);
    sp->allocs = prof_allocs_this_frame() - allocs0; /* the card's own (the frame's total also has the game's) */
    if (closed) {
        script_player_close(sp);
        return 1;
    }
    return sp->active ? 0 : 1;
}
