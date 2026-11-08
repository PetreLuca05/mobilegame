/* Day/night cycle on the device's local clock (no network time).
 * The sun follows a fixed path - rises 06:30 in the east, 70 deg at 13:00
 * in the south, sets 19:30 in the west; at night the moon is the key light.
 * Colours come from keyframes in assets/daycycle.json (see CLAUDE.md). */
#ifndef MG_DAYCYCLE_H
#define MG_DAYCYCLE_H

#include "core/mg_types.h"

#define DAY_MAX_KEYS 16

typedef struct DayKey {
    float t;
    HMM_Vec3 sun, ambient, horizon, zenith, fog, cloud; /* sun/ambient already * intensity */
    float fog_density, lamps;
} DayKey;

typedef struct DayCycle {
    DayKey keys[DAY_MAX_KEYS];
    int key_count;
    float sunrise, sunset, sun_max_elev; /* hours, degrees */
    HMM_Vec3 moon;                       /* colour * intensity */
    float sun_scale, ambient_scale;
    HMM_Vec3 ambient_floor;
    /* clock */
    bool follow_device; /* t tracks the phone's local time */
    float speed;        /* 1, 60, 600: game hours advance this many times faster */
    double t;           /* hours 0..24 */
    double next_clock_read;
} DayCycle;

/* What the renderer and lamps need at the current time. */
typedef struct DayState {
    HMM_Vec3 key_dir;    /* towards the key light (sun or moon) */
    HMM_Vec3 key_color;  /* colour * intensity */
    HMM_Vec3 ambient;
    HMM_Vec4 horizon, zenith;
    HMM_Vec3 fog;
    float fog_density;
    HMM_Vec3 cloud;
    float lamp_on;       /* 0..1 exterior lamps */
    HMM_Vec4 sun_disc;   /* xyz direction, w visibility */
    HMM_Vec4 moon_disc;
    float stars;         /* 0..1 */
} DayState;

/* Loads assets/daycycle.json (built-in defaults if missing/invalid). */
void daycycle_init(DayCycle *d);
/* Hours since local midnight on this device. */
double daycycle_device_hours(void);
/* Per frame: advances the clock (wall-clock seconds `dt`). */
void daycycle_update(DayCycle *d, double now, float dt);
DayState daycycle_state(const DayCycle *d, float hours);

#endif
