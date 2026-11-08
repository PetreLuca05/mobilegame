/* M5 floor peek: a thin storey selector on the left edge (top storey
 * first, 36 pt rows). Holding a row views that storey; a tap keeps it for
 * `hold_s` seconds, then the view eases back to the player's storey (the
 * floor fade does the easing). View only: the elevator still moves you. */
#ifndef MG_FLOORPEEK_H
#define MG_FLOORPEEK_H

#include "widgets.h"

typedef struct FloorPeek {
    int peek;      /* storey being viewed, -1 = none */
    double until;  /* tap: peek ends at this time */
    bool held;
} FloorPeek;

/* Draws the strip at x (pt), vertically centred in h_pt. `labels[i]` is the
 * row text for storey i. Returns the view floor override, or -1. */
int floorpeek_update(FloorPeek *fp, Ui *ui, float x, float h_pt, int floors, int player_floor,
                     const char *const *labels, double now, float hold_s);

#endif
