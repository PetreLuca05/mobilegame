/* --autotest avatar (LIB-1 A1): the pose function and the parts, measured on avatar_solve (no screen needed).
 *   assembly  the head centre 0.86 m up the body's own axis at lean 0 and 0.30 rad; the hands at rest when idle
 *   walk      3 s at 4.5 m/s: the bob peaks at 3.5 cm +- 0.5, the hands swing in opposite phase; a 90 deg turn
 *             settles (within 3 deg) in 0.2 .. 0.5 s with no frame turning more than 15 deg
 *   options   200 samples of 2 x 8 x 6 x 8 x 8 x 5: every part drawn exists, hair and head extras reach the head (nearest vertex <= 3 cm outside it)
 *   skins     old profiles.skin rows (M2 palette-only, v2 partial, v2 full) parse to the same choices
 * The pictures (option sheet, ages, seats, bed, poses, x-ray, previews) come from tools/avatar_test.sh. */
#ifndef MG_AUTOTEST_AVATAR_H
#define MG_AUTOTEST_AVATAR_H

#include <SDL3/SDL.h>

#include "platform/app.h"

struct AutotestProbe;
struct AutotestOut;

typedef struct AvatarTest {
    int stage;
    char why[200];
} AvatarTest;

int at_run(AvatarTest *t, MgApp *app, const struct AutotestProbe *p, struct AutotestOut *out, double now);

#endif
