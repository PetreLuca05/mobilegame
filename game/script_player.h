/* BOOK-ENGINE S5: the script player. One modal card (scrim, no blur) that plays a script's steps in order: predict (an item
 * before teaching; a wrong answer is fine and sets nothing), teach (the atoms' text with the printed page, the figure) and
 * check (an item; answered right = its facts are set through book_progress_set_fact). Widgets for the 8 item types: simplu
 * (one tap), multiplu (toggles + confirm), cloze (a chip row from the item and its siblings fills the gaps), card (flip, then
 * "I knew it" / "I did not"), sort (chips dragged or tapped into 2-3 bins), match (tap left, then right), order (rows dragged
 * into place), label (the figure with blank boxes from the pack's figlabels, labels dragged or tapped onto them). After an
 * answer: the explanation and, for multiplu, each false option's why. Every pack text wraps and scrolls inside the card,
 * nothing is cut. Strings from the pack, chrome from assets/strings*.json. The card is part of the one UI draw (the figure is
 * the UI shader's third texture). No allocation: fixed buffers in the struct, the figures read in place.
 *
 * Widget ids (ui_find, autotests): sp_close, sp_next (Continue / Done), sp_confirm (multiplu, cloze, sort, match, order,
 * label), sp_flip / sp_knew / sp_didnt (card), sp_opt<i> (simplu / multiplu option i of the item), sp_cand<i> (cloze
 * candidate i), sp_gap<g> (a filled gap: tap clears), sp_ent<i> / sp_bin<b> / sp_pool (sort), sp_ml<i> / sp_mr<i> (match,
 * by pair), sp_ord<i> (order, by sequence position), sp_lab<i> / sp_box<i> (label i and the box it belongs on). */
#ifndef MG_SCRIPT_PLAYER_H
#define MG_SCRIPT_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "book_figs.h"
#include "book_pack.h"
#include "book_progress.h"
#include "platform/app.h"
#include "ui/widgets.h"

#define SP_LINES 2048 /* laid-out lines of one step (the longest teach step of the pack is a few hundred) */
#define SP_BLOCKS 128
#define SP_OPTS 16    /* options / entries / pairs / labels of one item the widgets take */
#define SP_CANDS 8    /* cloze candidates */
#define SP_GAPS 6

enum { SP_PREDICT, SP_TEACH, SP_CHECK };                                                         /* step stage */
enum { SP_SIMPLU, SP_MULTIPLU, SP_CARD, SP_CLOZE, SP_SORT, SP_MATCH, SP_ORDER, SP_LABEL, SP_TYPES }; /* item type */

typedef struct SpBlock {
    const char *s;
    uint32_t len;
    float w, size;
    int face;
    int first, count;
} SpBlock;

typedef struct ScriptPlayer {
    bool active, finished;
    const BookPack *pack;
    GfxContext *gfx;
    BookFigs figs;
    bool figs_ok;
    int fig_resident; /* figs row on the GPU, -1 none */
    int fig_shown;    /* figs row this step wants, -1 none */
    uint32_t script;
    char presenter[48];
    int step;         /* within the script */
    int stage, type;  /* this step's stage; its item's type (-1 for teach) */
    const mgpk_item_t *it;
    uint32_t item;
    int n;            /* options / entries / pairs / labels in play (<= SP_OPTS) */
    bool answered, right, reveal;
    int checks, checks_answered, checks_right;
    double step_t;    /* app time the step opened */
    /* per item */
    uint32_t sel;              /* multiplu toggles; simplu: the picked bit */
    int8_t place[SP_OPTS];     /* sort: bin of entry i; label: box of label i; match: right pair of left i; -1 none */
    uint8_t perm[SP_OPTS];     /* order: opt at each position; match: right column; sort / label: chip order */
    int n_groups;              /* sort bins */
    uint32_t group_text[3];
    int entry_group[SP_OPTS];  /* sort: the right bin of each entry */
    int n_boxes;               /* label: boxes = the item's labels; box i is where label i belongs */
    float box[SP_OPTS][4];     /* in the crop's pixels: x0 y0 x1 y1 */
    float crop_w, crop_h;
    int cand_opt[SP_CANDS];    /* cloze candidates: opt rows (absolute) */
    int n_cands, n_gaps;
    int8_t gap_fill[SP_GAPS];  /* candidate in each gap, -1 empty */
    int selected;              /* tap-select: the chip / left item held for the next tap, -1 */
    bool flipped;
    double flip_t;
    /* drag (sort, label, order) */
    int drag;                  /* chip being dragged, -1 */
    int drag_touch;
    float grab_x, grab_y, drag_w, drag_h;
    uint32_t drag_key[SP_OPTS];
    float chip_rect[SP_OPTS][4]; /* last frame's chips (drag start) */
    float drop_rect[SP_OPTS + 1][4];
    int n_drop;
    /* layout */
    UiScroll scroll;
    float card[4], body[4];
    float last_w;
    SpBlock blocks[SP_BLOCKS];
    int n_blocks, n_lines;
    TextLine lines[SP_LINES];
    char cloze_buf[2048];
    /* what the card emitted last frame (autotests) */
    int seen_from, seen_to;
    uint32_t inst_from, inst_to;
    bool fig_drawn, fig_placeholder;
    uint32_t allocs;           /* heap allocations the card made last frame (prof) */
    char why[96];              /* the last failure the player logged ("" none) */
    /* autotests (missions, book): answer every step by the pack's key once the card has shown */
    bool autoplay;
    int skip_fact;             /* autoplay answers wrong the items that hold this fact, then forgets it; -1 none */
} ScriptPlayer;

/* Once at startup: opens the embedded figures (content/figures.mgfig) and reserves the GPU texture for the largest. */
void script_player_init(ScriptPlayer *sp, GfxContext *g);
/* Opens script `script_index` of the pack; presenter_name heads the card. false (and nothing opens) on a bad index. */
bool script_player_open(ScriptPlayer *sp, const BookPack *pack, uint32_t script_index, const char *presenter_name);
bool script_player_active(const ScriptPlayer *sp);
/* Each frame while active, last in the UI (it is a modal): draws and takes input. Returns 1 on the frame the script ended
 * (finished or closed; the caller commits progress), else 0. */
int script_player_frame(ScriptPlayer *sp, Ui *ui, MgApp *app, BookProgress *progress, double now_ms);
/* True if every check item of the script was answered (closed early = false). */
bool script_player_finished(const ScriptPlayer *sp);
void script_player_close(ScriptPlayer *sp);
/* Autotests: answers the current step through the player's own grading (answer()) with the pack's key, or wrong; a teach
 * step just goes on. Returns false when the script has ended. */
bool script_player_answer_key(ScriptPlayer *sp, BookProgress *progress, double now_ms, bool wrong);

#endif
