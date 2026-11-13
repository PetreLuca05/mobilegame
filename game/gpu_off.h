/* --gpu-off PART[,PART...] (test / perf only, read once at start, never saved): each part's GPU work switched off at its draw
 * sites (gfx_set_filter or the material), so the frame's CPU work stays comparable. For the iPad GPU breakdowns. */
#ifndef MG_GPU_OFF_H
#define MG_GPU_OFF_H

#include <stdint.h>

enum {
    GPU_OFF_MSAA = 1u << 0,             /* = --no-msaa */
    GPU_OFF_ALPHA = 1u << 1,            /* menu: every GFX_MAT_ALPHA draw of the library */
    GPU_OFF_PARTICLES = 1u << 2,        /* menu: the puff pool's draw */
    GPU_OFF_PEOPLE = 1u << 3,           /* menu: every avatar in the library (background people, members, the hero) */
    GPU_OFF_PEOPLE_LOD1 = 1u << 4,      /* menu: every library avatar at LOD 1, no crossfades */
    GPU_OFF_BACKBAYS = 1u << 5,         /* menu: everything of bays 2 and 3 (the back row) */
    GPU_OFF_FLAT = 1u << 6,             /* menu: the library's toon draws (not avatars) through the unlit pipeline */
    GPU_OFF_PROPS = 1u << 7,            /* island: the prop groups */
    GPU_OFF_TERRAIN_FLAT = 1u << 8,     /* island: the terrain mesh through the unlit pipeline */
    GPU_OFF_LIBRARY_BUILDING = 1u << 9, /* island: the library building drawn on the island */
    GPU_OFF_SEA = 1u << 10,             /* island: the sea */
};

#endif
