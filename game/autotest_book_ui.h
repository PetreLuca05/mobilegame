/* --autotest book_ui (BOOK-ENGINE S8): starts in the institute, offline, and plays a script for each of the 8 item types
 * through the script player, twice: answered wrong first (none of the item's unset facts may be set), then right (every
 * fact of the item set). Answers go through real touches on widget ids (ui_find), with one real drag for sort, order and
 * label; a widget scrolled out of the card is scrolled to with a drag of the body. Pictures book_<window>_<type>[_ok|_bad]
 * and book_<window>_teach (tools/book_ui_test.sh runs the four windows and copies them to docs/autotest/book/). Fails on a
 * target under 44 pt or outside the safe rect, more than 3 UI draws or any heap allocation in a frame with the card up,
 * a label figure that is neither drawn nor a placeholder, or a script that does not end finished. */
#ifndef MG_AUTOTEST_BOOK_UI_H
#define MG_AUTOTEST_BOOK_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/app.h"

struct AutotestProbe;
struct AutotestOut;

#define BT_OPS 48

typedef struct BtOp {
    int kind;        /* 1 tap, 2 drag, 3 wait */
    char a[24], b[24];
} BtOp;

typedef struct BookUiTest {
    int stage, run, step_seen, op, n_ops, sub, finger;
    int script[8];   /* a script whose check item has type i, -1 none */
    double at, t0, step_at;
    bool wrong;      /* this run answers wrong */
    BtOp ops[BT_OPS];
    /* scroll search */
    int scrolls, dir;
    float last_off;
    float fx, fy, tx, ty;
    int motion;
    /* checks */
    uint8_t facts_before[64]; /* the item's facts, set before the run (bit per ref) */
    uint32_t worst_ui_draws, allocs, frames, checked, other_allocs;
    int small, outside, drags, figs, placeholders;
    char win[16];
    char why[200];
} BookUiTest;

/* 1 passed, -1 failed (why), 0 running */
int bu_run(BookUiTest *t, MgApp *app, const struct AutotestProbe *p, struct AutotestOut *out, double now);

#endif
