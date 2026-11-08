/* Tunables exposed in the debug menu. */
#ifndef MG_SETTINGS_H
#define MG_SETTINGS_H

#include "core/mg_types.h"
#include "gfx/gfx.h"

typedef struct GameSettings {
    float yaw_deg_per_pt;   /* 0.25 */
    float pitch_deg_per_pt; /* 0.2 */
    bool invert_y;
    float cam_distance;     /* 3.2 m */
    float cam_pitch_deg;    /* -20; slider value applied to the camera */
    float fov_deg;          /* 70 */
    float dead_zone_pt;     /* 60 */
    bool show_colliders;
    bool show_triggers;     /* elevator trigger volume */
    int teleport_floor;     /* MENU_TELEPORT target */
    float render_scale;     /* 3D resolution vs native (UI stays native) */
    char room_code[5];      /* last entered party code */
} GameSettings;

static inline void settings_default(GameSettings *s)
{
    s->yaw_deg_per_pt = 0.25f;
    s->pitch_deg_per_pt = 0.2f;
    s->invert_y = false;
    s->cam_distance = 3.2f;
    s->cam_pitch_deg = -20.0f;
    s->fov_deg = 70.0f;
    s->dead_zone_pt = 60.0f;
    s->show_colliders = false;
    s->show_triggers = false;
    s->teleport_floor = 0;
    s->render_scale = gfx_default_render_scale();
    s->room_code[0] = 0;
}

#endif
