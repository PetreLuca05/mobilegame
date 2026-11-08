/* Tunables exposed in the debug menu. */
#ifndef MG_SETTINGS_H
#define MG_SETTINGS_H

#include "core/mg_types.h"
#include "gfx/gfx.h"

typedef struct GameSettings {
    float yaw_deg_per_pt;   /* 0.25 */
    float pitch_deg_per_pt; /* 0.2 */
    bool invert_y;
    bool show_storeys;      /* colour meshes by storey id (debug) */
    bool show_floor_peek;   /* storey selector on the left edge */
    float floor_peek_hold;  /* s a tapped peek lasts: 0, 3, 10 */
    bool show_own_name;     /* name tag over the local player */
    bool show_xray;         /* players' silhouettes through walls (M5-X) */
    bool show_nav;          /* debug: walkability grid + last path */
    bool xray_always;       /* debug: x-ray ignores depth */
    float cam_pitch_deg;    /* third-person: -20; slider value applied to the camera */
    float fov_deg;          /* 55 (landscape) */
    float dead_zone_pt;     /* 60 */
    bool show_colliders;
    bool show_triggers;     /* elevator trigger volume */
#ifdef MG_DAYCYCLE_CONTINUOUS
    bool follow_clock;      /* day cycle follows the device's local time */
    float time_of_day;      /* hours, shown / set by the Time slider */
    float time_speed;       /* 1, 60, 600 */
#endif
#ifdef MG_RENDER_FULL
    bool show_lights;       /* light gizmos + light counts */
    int tier_override;      /* 0 auto (RAM), 1 low, 2 high */
    bool shadows_on;
    bool show_cascades;     /* colour per sun cascade */
    bool freeze_shadows;
#endif
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
    s->show_storeys = false;
    s->show_floor_peek = true;
    s->floor_peek_hold = 3.0f;
    s->show_own_name = false;
    s->show_xray = true;
    s->xray_always = false;
    s->cam_pitch_deg = -20.0f;
    s->fov_deg = 55.0f;
    s->dead_zone_pt = 60.0f;
    s->show_colliders = false;
    s->show_triggers = false;
    s->teleport_floor = 0;
#ifdef MG_RENDER_FULL
    s->tier_override = 0;
    s->shadows_on = true;
    s->show_cascades = false;
    s->freeze_shadows = false;
#endif
    s->sound_on = true;
    s->render_scale = gfx_default_render_scale();
    s->room_code[0] = 0;
}

#endif
