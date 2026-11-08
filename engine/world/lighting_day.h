/* The game's lighting: always day, one set of constants (M5b replaced by
 * "no day/night at all"). The continuous cycle lives on behind
 * MG_DAYCYCLE=continuous (engine/world/daycycle.c), not shipped. */
#ifndef MG_LIGHTING_DAY_H
#define MG_LIGHTING_DAY_H

#include "core/mg_types.h"

/* What the renderer and the sky need. */
typedef struct DayState {
    HMM_Vec3 key_dir;    /* towards the key light */
    HMM_Vec3 key_color;  /* colour * intensity */
    HMM_Vec3 ambient;
    HMM_Vec4 horizon, zenith;
    HMM_Vec3 fog;
    float fog_density;
    HMM_Vec3 cloud;
    float lamp_on;       /* 0..1 (continuous cycle only; fixed: 0) */
    HMM_Vec4 sun_disc;   /* xyz direction, w visibility */
    HMM_Vec4 moon_disc;
    float stars;         /* 0..1 */
} DayState;

/* Constant emissive boost of lamp meshes: they look like lamps, light nothing. */
#define LAMP_DAY_EMISSIVE 0.3f

DayState lighting_day(void);

#endif
