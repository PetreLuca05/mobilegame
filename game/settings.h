/* Tunables exposed in the debug menu. */
#ifndef MG_SETTINGS_H
#define MG_SETTINGS_H

#include "core/mg_types.h"
#include "camera_td.h"
#include "gfx/gfx.h"

typedef struct GameSettings {
    float yaw_deg_per_pt;   /* 0.25 */
    float pitch_deg_per_pt; /* 0.2 */
    bool invert_y;
    bool show_storeys;      /* colour meshes by storey id (debug) */
    float td_fov_deg;       /* top-down camera: vertical field of view, 32 */
    float td_distance;      /* metres from the player, 16 */
    float td_pitch_deg;     /* degrees down from the horizon, 62 */
    bool show_own_name;     /* name tag over the local player */
    bool show_xray;         /* players' silhouettes through walls (M5-X) */
    bool show_nav;          /* debug: walkability grid + last path */
    bool show_stats;        /* the fps / position text, top left */
    bool show_profiler;     /* debug: scopes and counters on the HUD */
    bool show_slots;        /* debug: NPC slots */
    bool show_safe_rect;    /* debug: the safe rect and the insets */
    bool force_lift_panel;  /* debug */
    bool show_packages;     /* debug: archetype + step over each NPC */
    bool show_npc_paths;    /* debug */
    bool freeze_npcs;       /* debug */
    bool respawn_npcs;      /* debug: one shot */
    float npc_density;      /* 0 off, 0.5 low, 1 normal */
    int chat_bubbles;       /* 0 off, 1 the active NPC, 2 nearby ones too */
    float stress_count;     /* debug: wandering dummy avatars, 0..400 */
    bool haptics;           /* marker taps (iOS) */
    bool marker_sounds;
    bool test_markers;      /* debug: scatter markers to tap */
    float test_marker_count; /* 1..16 */
    bool show_all_paths;    /* debug: a path preview for every marker */
    bool xray_always;       /* debug: x-ray ignores depth */
    float cam_pitch_deg;    /* third-person: -20; slider value applied to the camera */
    float fov_deg;          /* 55 (landscape) */
    float dead_zone_pt;     /* 30 */
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
    s->show_stats = false;
    s->npc_density = 1.0f;
    s->chat_bubbles = 2;
    s->td_fov_deg = CAM_FOV_DEG;
    s->td_distance = CAM_DISTANCE;
    s->td_pitch_deg = CAM_PITCH_DEG;
    s->show_own_name = false;
    s->show_xray = true;
    s->haptics = true;
    s->marker_sounds = true;
#ifdef NDEBUG
    s->test_markers = false;
#else
    s->test_markers = true;
#endif
    s->test_marker_count = 8.0f;
    s->show_all_paths = false;
    s->xray_always = false;
    s->cam_pitch_deg = -20.0f;
    s->fov_deg = 55.0f;
    s->dead_zone_pt = 30.0f;
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
