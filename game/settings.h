/* Tunables exposed in the debug menu. */
#ifndef MG_SETTINGS_H
#define MG_SETTINGS_H

#include "core/mg_types.h"
#include "gfx/gfx.h"

typedef struct GameSettings {
    float yaw_deg_per_pt;   /* 0.25 */
    float pitch_deg_per_pt; /* 0.2 */
    bool invert_y;
    float cam_distance;     /* top-down: 16 m (10-24) */
    bool cam_yaw_lock;      /* top-down: always look north */
    bool cam_snap;          /* top-down: 45 deg snaps on release (debug) */
    float cam_pitch_override; /* top-down pitch, deg below horizontal, 62 (debug 45-89) */
    float cam_fov_td;       /* top-down vertical FOV, 32 (debug) */
    bool show_pivot;        /* top-down pivot marker (debug) */
    float cam_pitch_deg;    /* third-person: -20; slider value applied to the camera */
    float fov_deg;          /* 55 (landscape) */
    float dead_zone_pt;     /* 60 */
    bool show_colliders;
    bool show_triggers;     /* elevator trigger volume */
    bool show_lights;       /* light gizmos + light counts */
    bool follow_clock;      /* day cycle follows the device's local time */
    float time_of_day;      /* hours, shown / set by the Time slider */
    float time_speed;       /* 1, 60, 600 */
    int tier_override;      /* 0 auto (RAM), 1 low, 2 high */
    bool shadows_on;
    bool show_cascades;     /* colour per sun cascade */
    bool freeze_shadows;
    bool sound_on;
    int teleport_floor;     /* MENU_TELEPORT target */
    float render_scale;     /* 3D resolution vs native (UI stays native) */
    char room_code[5];      /* last entered party code */
} GameSettings;

static inline void settings_default(GameSettings *s)
{
    s->yaw_deg_per_pt = 0.25f;
    s->pitch_deg_per_pt = 0.2f;
    s->invert_y = false;
    s->cam_distance = 16.0f;
    s->cam_yaw_lock = false;
    s->cam_snap = true;
    s->cam_pitch_override = 62.0f;
    s->cam_fov_td = 32.0f;
    s->show_pivot = false;
    s->cam_pitch_deg = -20.0f;
    s->fov_deg = 55.0f;
    s->dead_zone_pt = 60.0f;
    s->show_colliders = false;
    s->show_triggers = false;
    s->teleport_floor = 0;
    s->tier_override = 0;
    s->shadows_on = true;
    s->show_cascades = false;
    s->freeze_shadows = false;
    s->sound_on = true;
    s->render_scale = gfx_default_render_scale();
    s->room_code[0] = 0;
}

#endif
