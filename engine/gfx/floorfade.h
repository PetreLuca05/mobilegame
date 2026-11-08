/* M5 floor fade timing: a handful of visibility values (storeys, wall
 * cutaways, tree canopies; the scene decides what each slot means) that
 * move towards their target 0..1 over 350 ms. Drawn with
 * GfxMaterial.hide = floorfade_hide(): a screen-door dither, no blending. */
#ifndef MG_FLOORFADE_H
#define MG_FLOORFADE_H

#include "core/mg_types.h"

#define FLOORFADE_TIME 0.35f
#define FLOORFADE_SLOTS 16

typedef struct FloorFade {
    float vis[FLOORFADE_SLOTS];    /* current, 1 drawn .. 0 gone */
    float target[FLOORFADE_SLOTS];
} FloorFade;

void floorfade_init(FloorFade *f); /* everything visible */
void floorfade_update(FloorFade *f, float dt);
void floorfade_snap(FloorFade *f); /* jump to the targets (load, teleport) */
/* hide value for a material: 1 - vis, culled entirely (1) at vis <= 0.02 */
static inline float floorfade_hide(float vis) { return vis <= 0.02f ? 1.0f : 1.0f - vis; }

#endif
