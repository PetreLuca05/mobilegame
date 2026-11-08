/* Game: scene (hospital or dev room), kinematic player, landscape touch
 * controls, diagonal top-down camera (third-person with
 * MG_CAMERA=thirdperson), HUD, settings menu and multiplayer session.
 * Keyboard/mouse also work on desktop. */
#include "game.h"

#include <math.h>
#include <stdlib.h> /* _Exit: autotest save */

#include "audio/audio.h"
#ifdef MG_DAYCYCLE_CONTINUOUS
#include "world/daycycle.h"
#else
#include "world/lighting_day.h"
#endif
#include "platform/google_auth.h"
#include "autotest.h"
#include "core/jobs.h"
#include "core/prof.h"
#include "perf_run.h"
#include "platform/device_stats.h"
#include "stress.h"
#include "ui/prof_hud.h"
#include "avatar.h"
#include "camera_td.h"
#include "gfx/floorfade.h"
#include "camera_tp.h"
#include "config.local.h"
#ifndef MG_VERSION
#define MG_VERSION "0.4.0"
#endif
#include "core/log.h"
#ifndef MG_GOOGLE_WEB_CLIENT_ID /* older config.local.h */
#define MG_GOOGLE_WEB_CLIENT_ID ""
#endif
#ifndef MG_GOOGLE_IOS_CLIENT_ID
#define MG_GOOGLE_IOS_CLIENT_ID ""
#endif
#include "elevator.h"
#include "floor_menu.h"
#include "frontend.h"
#include "modes.h"
#include "prefs.h"
#include "save.h"
#include "strings.h"
#include "gfx/camera.h"
#include "gfx/material.h"
#include "gfx/mesh.h"
#include "net_session.h"
#include "player.h"
#include "autonav.h"
#include "markers.h"
#include "nav_debug.h"
#include "platform/haptics.h"
#include "ui/marker_hud.h"
#include "route_test.h"
#include "scene.h"
#include "scenery.h"
#include "settings.h"
#include "settings_menu.h"
#include "stats.h"
#include "ui/hud.h"
#include "ui/touch_controls.h"

typedef struct Game {
    Scene *scene;
    ColWorld world;
    KinematicParams kparams;
    Player player;
    TpCamera cam;          /* MG_CAMERA=thirdperson only */
    TdCamera td;           /* the default camera */
    FloorFade fade;        /* storeys / wall cutaway / canopies (scene fade slots) */
    bool riding;           /* in the moving elevator car */
    double start_time;     /* first frame, for --screenshot-at */
    uint32_t shots_taken;  /* --screenshot-at entries done (bit per entry) */
    uint32_t occl_frames, occl_hidden, occl_lost; /* route test: indoor frames / behind a wall / ... with no x-ray */
    struct NavWorld *nav;  /* pathfinding grids, NULL if the scene has none */
    Markers markers;
    uint32_t active_marker; /* the one auto-nav is walking to, 0 none */
    double marker_spawn_at; /* test markers: no new one before this */
    uint32_t markers_reached;
    bool log_remote;
    double next_remote_log;
    bool marker_args_done;
    int marker_override;    /* --markers N / autotest, -1 = use the setting */
    MarkerHudIn hud_in[MARKER_HUD_MAX];
    MarkerHudOut hud_out[MARKER_HUD_MAX];
    int hud_count;
    NavPath *all_paths;     /* debug preview, MARKER_MAX */
    int all_paths_next;
    Autonav autonav;
    float auto_x, auto_z;  /* this frame's synthetic stick */
    NavPath nav_path;      /* last path found */
    bool nav_path_ok;
    NavDebug nav_debug;
    HMM_Mat4 view_proj;    /* this frame's camera, for name tags */
    float ride_offset;     /* feet height above the car floor */
    bool fade_snap;
    FlyCamera fly;
    bool fly_mode;
    GameSettings settings;
    TouchControlsConfig touch_cfg;
    TouchControls touch;
    Ui ui;
    SettingsMenu menu;
    AvatarAnim preview_anim;
    char account_msg[96];
    GfxMeshId unit_box;
    GfxMeshId ref_capsule; /* 1.8 m x 0.35 m player-sized ghost for the collider view */
    Scenery scenery;
    AvatarMeshes avatar_meshes;
    AvatarAnim local_anim;
    AvatarLook local_look;
    NetTransport *net;
    NetSession session;
    bool demo_walk;
    Frontend fe;
    Prefs prefs;
    char profile_name[32]; /* desktop --profile */
    bool playing;          /* in the hospital (else the front end) */
    bool play_paused;      /* front end opened from the game (edit character) */
    int load_step;
    int pending_play;      /* what LOADING leads to: 0 new game, 1 create room, 2 join room, 3 continue */
    char status[96];
    Autotest autotest;
    PerfRun perf;
    StressCrowd crowd;
    bool crowd_ok, prof_on, no_lod;
    int msaa_samples;           /* what the thermal path returns to */
    int thermal, thermal_force; /* thermal_force: --force-thermal N, -1 = ask the OS */
    double next_thermal_poll;
    uint32_t rendered_entities, rendered_last; /* this frame / the finished one */
    Audio *audio;
    RouteTest route;
    Stats stats;
#ifdef MG_DAYCYCLE_CONTINUOUS
    DayCycle day;
#endif
#ifdef MG_RENDER_FULL
    int tier;              /* 1 low, 2 high (applied) */
    GfxShadowConfig shadow_cfg; /* last applied */
    int shadowed_lights;  /* shadow-casting lights this frame */
#endif
    DayState day_state;
    float lamp_on;        /* continuous cycle only; fixed: 0 */
    NetProfile profile;    /* our account (name, skin, guest/Google) */
    bool profile_loaded;
    GoogleAuth *google;
    bool google_busy;      /* sign-in sheet open or link request in flight */
    bool skin_override;
    bool has_save;         /* save_slot0.bin exists (Continue) */
    bool stats_queued;     /* this run's stats are (partly) in prefs.stats_queue */
    NetStats queued;       /* what of this run is already in the queue */
    bool offline_play;     /* New Game / Continue: saves, no room */
    int logged_fe;         /* last front-end screen written to the log */
    double next_save;
    SaveGame save;         /* loaded on Continue; progress kept here */
    uint32_t save_play_base, save_rides_base;
    char pending_name[64]; /* Apple full name from the first sign-in */    /* desktop --skin P,H,A: local only, never saved */
    NetSkin override_skin;
    int last_ride_count; /* elevator rides already counted */
} Game;

static bool parse_vec3_arg(int argc, char **argv, const char *flag, HMM_Vec3 *out)
{
    for (int i = 1; i + 1 < argc; i++) {
        if (SDL_strcmp(argv[i], flag) == 0) {
            float x, y, z;
            if (SDL_sscanf(argv[i + 1], "%f,%f,%f", &x, &y, &z) == 3) {
                *out = HMM_V3(x, y, z);
                return true;
            }
        }
    }
    return false;
}

static bool has_flag(int argc, char **argv, const char *flag)
{
    for (int i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], flag) == 0) {
            return true;
        }
    }
    return false;
}

static const char *arg_value(int argc, char **argv, const char *flag)
{
    for (int i = 1; i + 1 < argc; i++) {
        if (SDL_strcmp(argv[i], flag) == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

static bool parse_cam_arg(int argc, char **argv, FlyCamera *c)
{
    for (int i = 1; i + 1 < argc; i++) {
        if (SDL_strcmp(argv[i], "--cam") == 0) {
            float x, y, z, yaw, pitch;
            if (SDL_sscanf(argv[i + 1], "%f,%f,%f,%f,%f", &x, &y, &z, &yaw, &pitch) == 5) {
                c->pos = HMM_V3(x, y, z);
                c->yaw = HMM_AngleDeg(yaw);
                c->pitch = HMM_AngleDeg(pitch);
                return true;
            }
        }
    }
    return false;
}

/* The hospital (+ terrain) is only built for play: the menu runs in the
 * small locker room. Returns false if the scene cannot be built. */
static bool ensure_world(Game *g, MgApp *app)
{
    if (g->scene) {
        return true;
    }
    g->scene = scene_create(app->gfx, &app->perm, &g->world, &app->font);
    if (g->scene) {
        g->nav = scene_nav_bake(g->scene, &app->perm, &g->world);
        g->crowd_ok = g->nav && stress_init(&g->crowd, &app->perm);
        if (g->nav) {
            mg_log_info("nav: baked %d grids in %.1f ms", nav_grid_count(g->nav), (double)nav_bake_ms(g->nav));
        }
        arena_release_unused(&app->perm); /* the mesh builders' scratch: ~30 MB of dirty pages */
    }
    if (g->nav && !markers_init(&g->markers, app->gfx, &app->perm)) {
        return false;
    }
    g->all_paths = ARENA_PUSH(&app->perm, NavPath, MARKER_MAX);
    SceneryShape shape = scene_scenery_shape();
    return g->scene && scenery_init(&g->scenery, &shape, app->gfx, &app->perm);
}

bool game_init(MgApp *app)
{
    Game *g = ARENA_PUSH(&app->perm, Game, 1);
    app->game = g;
    const char *profile = arg_value(app->argc, app->argv, "--profile");
    SDL_snprintf(g->profile_name, sizeof(g->profile_name), "%s", profile ? profile : "");
    if (!strings_init(&app->perm) || !frontend_init(&g->fe, app->gfx, &app->perm)) {
        return false;
    }
    kinematic_default_params(&g->kparams);
    prefs_load(&g->prefs, g->profile_name);
    g->settings = g->prefs.settings; /* per device */
    g->has_save = save_exists(g->profile_name);
    touch_controls_default_config(&g->touch_cfg);
#if !PLAYER_SPRINT_ENABLED
    g->touch_cfg.sprint_at = 2.0f; /* never: no rim sprint, no knob highlight */
    g->touch_cfg.full_at = 0.75f;  /* walk speed from a 3/4 push on; slower below */
#endif
#ifdef MG_CAMERA_THIRDPERSON
    g->touch_cfg.camera_zone = true;
#endif
    if (!PLAYER_JUMP_ENABLED) {
        g->touch_cfg.jump_size_pt = 0.0f; /* no jump button */
    }
    touch_controls_reset(&g->touch);

    tpcam_init(&g->cam);
    tdcam_init(&g->td);
    floorfade_init(&g->fade);
    g->fly.pos = HMM_V3(0.0f, 8.0f, 11.5f);
    g->fly.pitch = HMM_AngleDeg(-32.0f);
    g->fly_mode = parse_cam_arg(app->argc, app->argv, &g->fly);
    g->settings.show_colliders = has_flag(app->argc, app->argv, "--colliders");
    g->settings.show_storeys = has_flag(app->argc, app->argv, "--storeys");
    g->settings.show_nav = has_flag(app->argc, app->argv, "--nav");
    g->log_remote = has_flag(app->argc, app->argv, "--log-remote");
#ifdef MG_RENDER_FULL
    g->settings.show_lights = has_flag(app->argc, app->argv, "--lights");
    const char *tier_arg = arg_value(app->argc, app->argv, "--tier");
    g->settings.tier_override = tier_arg ? (SDL_strcmp(tier_arg, "low") == 0 ? 1 : 2) : 0;
    g->settings.shadows_on = !has_flag(app->argc, app->argv, "--no-shadows");
#endif
    g->menu.open = has_flag(app->argc, app->argv, "--menu");
    const char *menu_tab = arg_value(app->argc, app->argv, "--menu");
    g->menu.tab = menu_tab && SDL_strcmp(menu_tab, "skin") == 0 ? 1 : 0;
    g->menu.card = menu_tab && SDL_strcmp(menu_tab, "pause") == 0;

    size_t mark = arena_mark(&app->perm);
    MeshBuilder mb;
    mb_init(&mb, &app->perm, 64, 64);
    mb_box(&mb, HMM_M4D(1.0f), HMM_V3(1, 1, 1), HMM_V4(1, 1, 1, 1));
    g->unit_box = mb_upload(&mb, app->gfx);
    arena_reset_to(&app->perm, mark);
    mb_init(&mb, &app->perm, 2048, 8192);
    mb_tapered_capsule(&mb, HMM_M4D(1.0f), 0.35f, 0.35f, 1.8f, 16, 24, HMM_V4(0.92f, 0.92f, 0.95f, 1));
    g->ref_capsule = mb_upload(&mb, app->gfx);
    arena_reset_to(&app->perm, mark);
    if (!avatar_meshes_init(&g->avatar_meshes, app->gfx, &app->perm)) {
        return false;
    }
    g->local_look = avatar_look_from_skin(AVATAR_DEFAULT_SKIN);
    g->profile.skin = AVATAR_DEFAULT_SKIN;
    g->profile.guest = true;
    const char *skin_arg = arg_value(app->argc, app->argv, "--skin");
    int v[6];
    if (skin_arg && SDL_sscanf(skin_arg, "%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
        /* --skin body,skin,hair,hair_color,suit,accessory */
        g->skin_override = true;
        g->override_skin = (NetSkin){.body = (uint8_t)v[0], .skin = (uint8_t)v[1], .hair = (uint8_t)v[2],
                                     .hair_color = (uint8_t)v[3], .suit = (uint8_t)v[4], .accessory = (uint8_t)v[5]};
        g->profile.skin = g->override_skin;
    }
    SDL_snprintf(g->profile.display_name, sizeof(g->profile.display_name), "Player");
    if (g->prefs.has_profile_cache && !g->skin_override) { /* offline launch: last known account */
        SDL_snprintf(g->profile.display_name, sizeof(g->profile.display_name), "%s", g->prefs.cache_name);
        g->profile.skin = g->prefs.cache_skin;
        g->profile.guest = SDL_strcmp(g->prefs.auth_provider, "google") != 0 &&
                           SDL_strcmp(g->prefs.auth_provider, "apple") != 0;
    }

    /* --autotest offline: an unresolvable host behaves like airplane mode */
    const char *atest = arg_value(app->argc, app->argv, "--autotest");
    perf_run_init(&g->perf, app->argc, app->argv);
    const bool no_network = atest && (SDL_strcmp(atest, "offline") == 0 || g->perf.active);
    NetConfig ncfg = {no_network ? "offline.invalid" : MG_SUPABASE_HOST, MG_SUPABASE_ANON_KEY, profile ? profile : "", 25, 3,
                      has_flag(app->argc, app->argv, "--public-test")};
    g->net = net_create(&app->perm, &ncfg);
    session_init(&g->session, g->net);
    session_set_identity(&g->session, g->profile.display_name, g->profile.skin);
    net_account_start(g->net); /* guest sign-in + profile/stats, also for offline play */
    const char *party = arg_value(app->argc, app->argv, "--party");
    if (party) {
        session_start(&g->session, MODE_PARTY, party);
    } else if (has_flag(app->argc, app->argv, "--world")) {
        session_start(&g->session, MODE_WORLD, NULL);
    }
    /* the front end comes first unless a test flag already chose to play */
    bool play_now = party || has_flag(app->argc, app->argv, "--world") || has_flag(app->argc, app->argv, "--offline") ||
                    autotest_starts_in_game(app->argc, app->argv) || arg_value(app->argc, app->argv, "--spawn") ||
                    arg_value(app->argc, app->argv, "--cam") || has_flag(app->argc, app->argv, "--menu") ||
                    has_flag(app->argc, app->argv, "--colliders");
    const char *fe_arg = arg_value(app->argc, app->argv, "--frontend"); /* desktop: start on a screen */
    g->fe.state = g->prefs.onboarding_done ? FE_MAIN_MENU : FE_WELCOME;
    if (fe_arg) {
        static const char *const names[] = {"welcome", "username", "creator", "menu", "settings", "freeroam"};
        for (int i = 0; i < 6; i++) {
            if (SDL_strcmp(fe_arg, names[i]) == 0) {
                g->fe.state = (FeState)i;
            }
        }
    }
    const char *play_arg = arg_value(app->argc, app->argv, "--play"); /* desktop: new | continue */
    if (play_arg && !play_now) {
        g->pending_play = SDL_strcmp(play_arg, "continue") == 0 ? 3 : 0;
        g->load_step = 0;
        g->fe.state = FE_LOADING;
    }
    if (play_now) {
        if (!ensure_world(g, app)) {
            return false;
        }
        HMM_Vec3 spawn = scene_spawn(g->scene, 0);
        perf_run_spawn(&g->perf, &spawn);
        parse_vec3_arg(app->argc, app->argv, "--spawn", &spawn);
        player_init(&g->player, spawn, scene_spawn_yaw(g->scene));
        g->playing = true;
        g->fe.state = FE_PLAY;
    }
    SDL_snprintf(g->status, sizeof(g->status), "offline");
    autotest_init(&g->autotest, app->argc, app->argv);
    const char *msaa_arg = arg_value(app->argc, app->argv, "--msaa");
    g->msaa_samples = has_flag(app->argc, app->argv, "--no-msaa") ? 1 : (msaa_arg ? SDL_atoi(msaa_arg) : GFX_DEFAULT_MSAA);
    gfx_set_msaa(app->gfx, g->msaa_samples);
    gfx_set_instancing(app->gfx, !has_flag(app->argc, app->argv, "--no-instancing"));
    g->no_lod = has_flag(app->argc, app->argv, "--no-lod");
    const char *force_thermal = arg_value(app->argc, app->argv, "--force-thermal");
    g->thermal_force = force_thermal ? SDL_atoi(force_thermal) : -1;
    g->logged_fe = -1;
    g->audio = audio_create(&app->perm);
#ifdef MG_DAYCYCLE_CONTINUOUS
    daycycle_init(&g->day);
    g->settings.follow_clock = true;
    g->settings.time_speed = 1.0f;
    const char *time_arg = arg_value(app->argc, app->argv, "--time");
    if (time_arg) { /* desktop: fixed hour for screenshots */
        g->day.follow_device = g->settings.follow_clock = false;
        g->day.t = SDL_atof(time_arg);
    }
#else
    g->day_state = lighting_day();
#endif
    g->google = google_auth_create(&app->perm, MG_GOOGLE_WEB_CLIENT_ID, MG_GOOGLE_IOS_CLIENT_ID);
    route_test_init(&g->route, app->argc, app->argv);
    g->demo_walk = has_flag(app->argc, app->argv, "--demo-walk");
    return g->unit_box != GFX_MESH_NONE;
}

void game_tick(MgApp *app)
{
    Game *g = (Game *)app->game;
    if (!g->playing) {
        return;
    }
    scene_tick(g->scene, &g->world, MG_TICK_DT);
    Elevator *el = scene_elevator(g->scene);
    bool moving = el && elevator_moving(el);
    if (moving && !g->riding) { /* the car leaves: is the player in it? */
        HMM_Vec3 p = g->player.kin.pos;
        g->riding = p.X >= el->x0 && p.X <= el->x1 && p.Z >= el->z0 && p.Z <= el->z1 &&
                    fabsf(p.Y - (float)el->from_floor * el->storey) < 0.5f;
        g->ride_offset = p.Y - (float)el->from_floor * el->storey;
    }
    if (!moving) {
        g->riding = false;
    }
    if (el && el->arrived) {
        el->arrived = false;
        audio_ding(g->audio);
        g->stats.rides++;
        stats_reset_position(&g->stats);
    }
    if (g->riding) { /* travel with the car: no physics through the slabs */
        g->player.prev_pos = g->player.kin.pos;
        g->player.kin.pos.Y = el->car_y + g->ride_offset;
        g->player.kin.vel = HMM_V3(0, 0, 0);
    } else {
        PROF_SCOPE("kinematic");
        player_tick(&g->player, &g->kparams, &g->world, MG_TICK_DT);
    }
    if (g->crowd_ok && g->nav) {
        PROF_SCOPE("npc");
        StressView sv = {g->player.kin.pos.X, g->player.kin.pos.Z, scene_indoors(g->scene, g->player.kin.pos) ? scene_floor_of(g->scene, g->player.kin.pos) : 0};
        stress_tick(&g->crowd, g->nav, app->jobs, &app->frame, &sv, MG_TICK_DT);
    }
    stats_tick(&g->stats, g->player.kin.pos, MG_TICK_DT);
    if (g->offline_play && scene_indoors(g->scene, g->player.kin.pos)) { /* rooms visited: storey x chunk */
        HMM_Vec3 p = g->player.kin.pos;
        int col = (int)mg_clampf((p.X + 15.0f) / 3.75f, 0.0f, 7.0f);
        int bit = scene_floor_of(g->scene, p) * 16 + col + (p.Z > -9.4f ? 8 : 0);
        g->save.visited_rooms |= 1ull << (bit & 63);
    }
}

static void update_fly_camera(Game *g, MgApp *app)
{
    MgInput *in = &app->input;
    float dt = mg_minf(app->frame_dt, 0.1f);
    const bool *k = in->key_down;
    float fwd = (k[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_S] ? 1.0f : 0.0f);
    float strafe = (k[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_A] ? 1.0f : 0.0f);
    float up = (k[SDL_SCANCODE_E] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_Q] ? 1.0f : 0.0f);
    float look_x = in->mouse_dx + g->touch.camera_dx;
    float look_y = in->mouse_dy + g->touch.camera_dy;
    look_x += ((k[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f)) * 240.0f * dt;
    look_y += ((k[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_UP] ? 1.0f : 0.0f)) * 240.0f * dt;
    fwd += g->touch.move_y;
    strafe += g->touch.move_x;
    g->fly.yaw -= HMM_AngleDeg(0.25f) * look_x;
    g->fly.pitch = mg_clampf(g->fly.pitch - HMM_AngleDeg(0.2f) * look_y, HMM_AngleDeg(-89.0f), HMM_AngleDeg(89.0f));
    HMM_Vec3 f = camera_forward(g->fly.yaw, g->fly.pitch);
    HMM_Vec3 r = HMM_NormV3(HMM_Cross(f, HMM_V3(0, 1, 0)));
    HMM_Vec3 v = HMM_AddV3(HMM_AddV3(HMM_MulV3F(f, fwd), HMM_MulV3F(r, strafe)), HMM_V3(0, up, 0));
    g->fly.pos = HMM_AddV3(g->fly.pos, HMM_MulV3F(v, 6.0f * dt));
}

static void draw_name_tags(Game *g, MgApp *app, int view_floor, int player_floor, HMM_Vec3 feet);
static void run_nav_check(Game *g, MgApp *app);
static void run_determinism_check(Game *g, MgApp *app);
static void markers_update(Game *g, MgApp *app, HMM_Vec3 feet);
static void marker_tapped(Game *g, MgApp *app, uint32_t id);
static void markers_hud(Game *g, MgApp *app, int view_floor, HMM_Vec3 feet, bool floor_menu_up);
static bool screen_to_floor(const Game *g, const MgApp *app, float x_pt, float y_pt, float y, HMM_Vec3 *out);

/* X-ray silhouette colour: the avatar's accent at full saturation x 0.85
 * (grey accents stay grey), so players can tell each other apart. */
static HMM_Vec4 xray_color(uint32_t accent)
{
    float r = (float)((accent >> 16) & 255) / 255.0f, gr = (float)((accent >> 8) & 255) / 255.0f,
          b = (float)(accent & 255) / 255.0f;
    float mx = mg_maxf(r, mg_maxf(gr, b)), mn = mg_minf(r, mg_minf(gr, b));
    if (mx - mn > 0.05f) { /* stretch to S = 1, keep hue and value */
        float k = mx / (mx - mn);
        r = (r - mn) * k;
        gr = (gr - mn) * k;
        b = (b - mn) * k;
    }
    return HMM_V4(r * 0.85f, gr * 0.85f, b * 0.85f, 1.0f);
}

/* M5-X: the hidden parts of an avatar - a 2 px dark rim, then the flat
 * accent fill (both dithered 60 %, depth GREATER; `always` ignores depth). */
static void draw_xray(Game *g, MgApp *app, const AvatarLook *look, const AvatarPose *pose, const AvatarAnim *anim,
                      bool always)
{
    GfxMaterial rim = mat_xray(HMM_V4(0.0f, 0.0f, 0.0f, 1.0f), true, always);
    GfxMaterial fill = mat_xray(xray_color(look->accent), false, always);
    avatar_draw_with(app->gfx, &g->avatar_meshes, look, pose, anim, &rim);
    avatar_draw_with(app->gfx, &g->avatar_meshes, look, pose, anim, &fill);
}

/* Touch stick / jump button, plus WASD, Shift, Space on desktop
 * (third-person: also camera drag, arrows and right-mouse drag). */
static void update_player_input(Game *g, MgApp *app)
{
    MgInput *in = &app->input;
    const GameSettings *s = &g->settings;
    const bool *k = in->key_down;
    float dt = mg_minf(app->frame_dt, 0.1f);

    float look_x = g->touch.camera_dx + in->mouse_dx;
#ifdef MG_CAMERA_THIRDPERSON
    float look_y = g->touch.camera_dy + in->mouse_dy;
    look_x += ((k[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f)) * 400.0f * dt;
    look_y += ((k[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_UP] ? 1.0f : 0.0f)) * 300.0f * dt;
    float invert = s->invert_y ? -1.0f : 1.0f;
    tpcam_orbit(&g->cam, -HMM_AngleDeg(s->yaw_deg_per_pt) * look_x, -HMM_AngleDeg(s->pitch_deg_per_pt) * look_y * invert);
    g->settings.cam_pitch_deg = HMM_ToDeg(g->cam.pitch);
    const float cam_yaw = g->cam.yaw;
#else
    /* fixed camera: stick up = screen up = north */
    const TdCamera *td = &g->td;
    const float cam_yaw = td->yaw;
    (void)dt, (void)look_x, (void)s;
#endif

    float fw = g->touch.move_y + (k[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_S] ? 1.0f : 0.0f);
    float st = g->touch.move_x + (k[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_A] ? 1.0f : 0.0f);
    float sy = HMM_SinF(cam_yaw), cy = HMM_CosF(cam_yaw); /* stick up = away from the camera */
    float mx = -sy * fw + cy * st;
    float mz = -cy * fw - sy * st;
    float len = HMM_SqrtF(mx * mx + mz * mz);
    if (len > 1.0f) {
        mx /= len;
        mz /= len;
    }
    /* real input wins at once; otherwise auto-nav's synthetic stick */
    if (autonav_active(&g->autonav)) {
        if (mx != 0.0f || mz != 0.0f) {
            autonav_cancel(&g->autonav);
        } else {
            mx = g->auto_x;
            mz = g->auto_z;
        }
    }
    bool sprint = g->touch.sprint || k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
    bool jump = g->touch.jump_held || k[SDL_SCANCODE_SPACE];
    if (g->demo_walk) { /* desktop test: walk circles and hop every few seconds */
        float a = (float)app->time * 0.9f;
        mx = HMM_CosF(a);
        mz = HMM_SinF(a);
        jump = SDL_fmodf((float)app->time, 3.0f) < 0.3f;
    }
    player_set_input(&g->player, mx, mz, sprint, jump);
}

static void save_prefs(Game *g)
{
    g->prefs.settings = g->settings;
    prefs_save(&g->prefs, g->profile_name);
}

/* Name/character changed: apply, write through, remember for offline launches. */
static void profile_changed(Game *g)
{
    session_set_identity(&g->session, g->profile.display_name, g->profile.skin);
    net_save_profile(g->net, g->profile.display_name, &g->profile.skin);
    g->prefs.has_profile_cache = true;
    SDL_snprintf(g->prefs.cache_name, sizeof(g->prefs.cache_name), "%s", g->profile.display_name);
    g->prefs.cache_skin = g->profile.skin;
    g->prefs.profile_stale = !g->profile_loaded; /* offline: push it on the next sign-in */
    save_prefs(g);
}

/* Offline game -> save_slot0.bin (position, floor, time, rides, rooms). */
static void write_save(Game *g)
{
    if (!g->playing || !g->offline_play || !g->scene) {
        return;
    }
    SaveGame *s = &g->save;
    s->pos = g->player.kin.pos;
#ifdef MG_CAMERA_THIRDPERSON
    s->yaw = g->player.yaw;
#else
    s->yaw = g->td.yaw; /* v2: the camera yaw (fixed since M5-S; read and ignored) */
#endif
    s->floor = (uint32_t)scene_floor_of(g->scene, g->player.kin.pos);
    s->play_seconds = g->save_play_base + (uint32_t)g->stats.play_seconds;
    s->elevator_rides = g->save_rides_base + (uint32_t)g->stats.rides;
    g->has_save = save_write(s, g->profile_name) || g->has_save;
}

/* Back to the main menu from the hospital (multiplayer: leave the room). */
static void leave_play(Game *g, MgApp *app)
{
    (void)app;
    write_save(g);
    session_say_goodbye(&g->session, 0, g->player.kin.pos, g->player.yaw);
    session_stop(&g->session);
    g->playing = false;
    g->offline_play = false;
    g->menu.card = false;
    autonav_clear(&g->autonav);
    g->active_marker = 0;
    marker_clear(&g->markers, MARKER_TEST);
    net_set_quiet(g->net, false);
    g->play_paused = false;
    g->menu.open = false;
    g->fe.depth = 0;
    g->fe.state = FE_MAIN_MENU;
    save_prefs(g);
}

static void handle_menu_action(Game *g, MgApp *app, MenuAction a)
{
    switch (a) {
    case MENU_SKIN:
    case MENU_NAME:
        if (a == MENU_NAME) {
            SDL_snprintf(g->profile.display_name, sizeof(g->profile.display_name), "%s", g->menu.name);
        }
        /* applies at once, written through to profiles, re-sent in Presence */
        session_set_identity(&g->session, g->profile.display_name, g->profile.skin);
        net_save_profile(g->net, g->profile.display_name, &g->profile.skin);
        break;
    case MENU_GOOGLE:
        g->account_msg[0] = 0;
        g->session.account_error[0] = 0;
        g->google_busy = google_auth_start(g->google, app->window);
        break;
    case MENU_SIGN_OUT:
        /* device settings and the save stay; the account cache does not */
        g->prefs.has_profile_cache = false;
        g->prefs.onboarding_done = false;
        g->prefs.auth_provider[0] = 0;
        save_prefs(g);
        net_sign_out(g->net);
        if (!g->playing) {
            g->menu.open = false;
            g->fe.depth = 0;
            g->fe.state = FE_WELCOME;
        }
        SDL_snprintf(g->account_msg, sizeof(g->account_msg), "Signed out - playing as a new guest");
        break;
    case MENU_DISCONNECT:
        session_say_goodbye(&g->session, 0, g->player.kin.pos, g->player.yaw);
        session_stop(&g->session);
        break;
    case MENU_CAMERA_PITCH:
        g->cam.pitch = HMM_AngleDeg(g->settings.cam_pitch_deg);
        break;
    case MENU_RESPAWN_MARKERS:
        marker_clear(&g->markers, MARKER_TEST);
        break;
#ifdef MG_DAYCYCLE_CONTINUOUS
    case MENU_TIME: /* slider: stop following the clock */
        g->day.t = g->settings.time_of_day;
        g->settings.follow_clock = false;
#endif
        break;
    case MENU_TELEPORT: {
        Elevator *el = scene_elevator(g->scene);
        int f = g->settings.teleport_floor;
        player_init(&g->player, scene_floor_lobby(g->scene, f), 0.0f);
        if (el) {
            elevator_place(el, f);
        }
        g->cam.initialized = g->td.initialized = false;
        g->fade_snap = true;
        break;
    }
    case MENU_MAIN_MENU:
        leave_play(g, app);
        break;
    case MENU_EDIT_CHARACTER:
        if (g->playing) { /* the world stays loaded; Done comes back here */
            g->playing = false;
            g->play_paused = true;
        }
        g->fe.depth = 0;
        g->fe.state = g->play_paused ? FE_PLAY : FE_SETTINGS;
        frontend_go(&g->fe, FE_CREATOR);
        break;
    case MENU_APPLE:
        g->account_msg[0] = 0;
        g->session.account_error[0] = 0;
        g->google_busy = apple_auth_start(g->google, app->window);
        break;
    default:
        break;
    }
}

static void draw_colliders(Game *g, GfxContext *gfx, HMM_Vec3 feet)
{
    GfxMaterial ghost = mat_ghost(HMM_V4(0.2f, 1.0f, 0.4f, 0.35f));
    for (int i = 0; i < g->world.count; i++) {
        const ColBox *b = &g->world.boxes[i];
        HMM_Mat4 m = HMM_M4D(1.0f);
        for (int c = 0; c < 3; c++) {
            m.Columns[c] = HMM_V4V(HMM_MulV3F(b->axis[c], b->half.Elements[c] + 0.002f), 0.0f);
        }
        m.Columns[3] = HMM_V4V(b->center, 1.0f);
        gfx_draw(gfx, g->unit_box, &m, &ghost, 0);
    }
    HMM_Mat4 pm = HMM_Translate(feet);
    GfxMaterial cap = mat_ghost(HMM_V4(1.0f, 0.85f, 0.2f, 0.5f));
    gfx_draw(gfx, g->ref_capsule, &pm, &cap, 0);
}

/* Light, sky and fog from the day cycle. */
static void scene_defaults(GfxSceneParams *sp, float time, const DayState *d)
{
    sp->sun_dir = d->key_dir;
    sp->sun_color = d->key_color;
    sp->ambient = d->ambient;
    sp->sky_horizon = d->horizon;
    sp->sky_zenith = d->zenith;
    sp->fog_color = d->fog;
    sp->fog_density = d->fog_density;
    sp->sun_disc = d->sun_disc;
    sp->moon_disc = d->moon_disc;
    sp->stars = d->stars;
#ifdef MG_RENDER_FULL
    sp->reflection_y = 0.0f;
    sp->reflection_strength = 1.0f;
#endif
    sp->time = time;
}

/* Big room badge at the top so the code can be read out to friends;
 * connection problems show here in red. */
static void draw_room_badge(Game *g, MgApp *app)
{
    const NetSession *s = &g->session;
    if (s->mode == MODE_OFFLINE && s->status != NET_FAILED) {
        return;
    }
    char line[128];
    bool bad = s->status == NET_FAILED || s->status == NET_RECONNECTING;
    if (s->mode == MODE_PARTY) {
        SDL_snprintf(line, sizeof(line), "Room %s", s->room_code);
    } else if (s->mode == MODE_WORLD) {
        SDL_snprintf(line, sizeof(line), "World %02d", s->shard);
    } else {
        SDL_snprintf(line, sizeof(line), "Offline");
    }
    float cx = app->width_pt * 0.5f;
    float y = app->safe_top + 104.0f; /* below the debug HUD lines */
    float tw = text_width(&app->font, line, 24.0f);
    ui_round_rect(app->gfx, &app->font, cx - tw * 0.5f - 18.0f, y, tw + 36.0f, 40.0f, 20.0f, ui_rgba(0x1E2230C0));
    text_draw_centered(app->gfx, &app->font, cx, y + 20.0f, 24.0f, ui_rgba(0xFFFFFFFF), line);
    if (s->status != NET_JOINED) {
        const char *msg = s->status_text;
        float mw = mg_minf(text_width(&app->font, msg, 14.0f), app->width_pt - 40.0f);
        ui_round_rect(app->gfx, &app->font, cx - mw * 0.5f - 12.0f, y + 46.0f, mw + 24.0f, 26.0f, 13.0f,
                      bad ? ui_rgba(0xD94141E0) : ui_rgba(0x1E2230A0));
        text_draw_centered(app->gfx, &app->font, cx, y + 59.0f, 14.0f, ui_rgba(0xFFFFFFFF), msg);
    }
}

#ifdef MG_RENDER_FULL
/* GPU tier from RAM (A12-class phones have <= 4 GB, A15+ 6 GB+) unless
 * overridden; sets shadow sizes, caps and lights per draw. */
static void apply_tier(Game *g, MgApp *app)
{
    const GameSettings *s = &g->settings;
    int tier = s->tier_override ? s->tier_override : (SDL_GetSystemRAM() >= 6000 ? 2 : 1);
    GfxShadowConfig c = tier == 2 ? (GfxShadowConfig){true, 2048, 4096, 2048, 8, 2, false, false}
                                  : (GfxShadowConfig){true, 1024, 2048, 1024, 4, 1, false, false};
    c.enabled = s->shadows_on;
    c.show_cascades = s->show_cascades;
    c.freeze = s->freeze_shadows;
    if (tier != g->tier || SDL_memcmp(&c, &g->shadow_cfg, sizeof(c)) != 0) {
        if (tier != g->tier) {
            mg_log_info("gfx: %s tier (%d MB RAM)", tier == 2 ? "high" : "low", SDL_GetSystemRAM());
        }
        g->tier = tier;
        g->shadow_cfg = c;
        gfx_set_shadows(app->gfx, &c);
        gfx_set_lights_per_draw(app->gfx, tier == 2 ? 8 : 4);
    }
}
#endif

/* What Settings -> Account shows. */
static SettingsAccount account_view(Game *g)
{
    SettingsAccount a = {g->profile.display_name,
                         g->profile.email,
                         g->profile.guest || !g->profile_loaded,
                         g->net && google_auth_available(g->google),
                         g->net && apple_auth_available(g->google),
                         g->prefs.auth_provider,
                         g->google_busy,
                         g->session.account_error[0] ? g->session.account_error : g->account_msg};
    return a;
}

/* Native sign-in sheet finished: hand the token to Supabase to link. */
static void poll_sign_in(Game *g, MgApp *app)
{
    char token[4096], nonce[96], err[160], name[64];
    char provider[12];
    GoogleAuthState gs = native_auth_poll(g->google, provider, sizeof(provider), token, sizeof(token), nonce,
                                          sizeof(nonce), name, sizeof(name), err, sizeof(err));
    if (gs == GOOGLE_DONE) {
        SDL_snprintf(g->prefs.auth_provider, sizeof(g->prefs.auth_provider), "%s", provider);
        if (name[0]) { /* Apple sends the full name only once: keep it now */
            SDL_snprintf(g->pending_name, sizeof(g->pending_name), "%s", name);
        }
        net_link_idp(g->net, provider, token, nonce);
        SDL_memset(token, 0, sizeof(token));
    } else if (gs == GOOGLE_FAILED) {
        g->google_busy = false;
        bool cancelled = SDL_strcmp(err, "cancelled") == 0;
        SDL_snprintf(g->account_msg, sizeof(g->account_msg), "%s", cancelled ? str("cancelled") : err);
        frontend_toast(&g->fe, cancelled ? str("cancelled") : str("no_connection"), app->time);
    }
    if (g->session.account_error[0] && g->google_busy) {
        g->google_busy = false;
        frontend_toast(&g->fe, str("no_connection"), app->time);
    }
}

/* Account (re)loaded from Supabase: cache it on the device and route the
 * welcome screen (a linked account with a name skips onboarding). */
static void account_loaded(Game *g, MgApp *app)
{
    (void)app;
    if (!g->profile.guest && g->pending_name[0] && SDL_strcmp(g->profile.display_name, "Player") == 0) {
        /* first link with Apple: its full name, cleaned to 2-16 safe chars */
        char name[NET_NAME_LEN];
        int n = 0;
        for (const char *c = g->pending_name; *c && n < 16; c++) {
            if (SDL_isalnum((unsigned char)*c) || *c == ' ' || *c == '_') {
                name[n++] = *c;
            }
        }
        name[n] = 0;
        if (n >= 2) {
            SDL_snprintf(g->profile.display_name, sizeof(g->profile.display_name), "%s", name);
            net_save_profile(g->net, g->profile.display_name, &g->profile.skin);
        }
    }
    g->pending_name[0] = 0;
    g->prefs.has_profile_cache = true;
    SDL_snprintf(g->prefs.cache_name, sizeof(g->prefs.cache_name), "%s", g->profile.display_name);
    g->prefs.cache_skin = g->profile.skin;
    if (g->profile.guest && !g->prefs.auth_provider[0]) {
        SDL_snprintf(g->prefs.auth_provider, sizeof(g->prefs.auth_provider), "guest");
    }
    if (g->fe.state == FE_WELCOME && !g->profile.guest && g->google_busy) {
        /* signed in from the welcome screen */
        if (SDL_strcmp(g->profile.display_name, "Player") != 0) {
            g->prefs.onboarding_done = true; /* known account: straight to the menu */
            g->fe.depth = 0;
            g->fe.state = FE_MAIN_MENU;
        } else {
            /* prefill: the provider's name (first word), else Player#### */
            int n = 0;
            for (const char *c = g->profile.google_name; *c && *c != ' ' && n < 16; c++) {
                if (SDL_isalnum((unsigned char)*c) || *c == '_') {
                    g->fe.name[n++] = *c;
                }
            }
            g->fe.name[n] = 0;
            if (n < 2) {
                SDL_snprintf(g->fe.name, sizeof(g->fe.name), "Player%04d", (int)SDL_rand(10000));
            }
            frontend_go(&g->fe, FE_USERNAME);
        }
    }
    g->google_busy = false;
    save_prefs(g);
}

/* LOADING: build the hospital over a few frames, then start playing. */
static void load_step(Game *g, MgApp *app)
{
    switch (g->load_step++) {
    case 0:
        break; /* one frame to show the bar */
    case 1:
        if (!ensure_world(g, app)) {
            frontend_toast(&g->fe, "Could not load the hospital", app->time);
            g->fe.state = FE_MAIN_MENU;
        }
        break;
    default:
        player_init(&g->player, scene_spawn(g->scene, 0), scene_spawn_yaw(g->scene));
        g->offline_play = g->pending_play == 0 || g->pending_play == 3;
        net_set_quiet(g->net, g->offline_play);
        g->save_play_base = g->save_rides_base = 0;
        if (g->pending_play == 3 && save_read(&g->save, g->profile_name)) { /* Continue */
            player_init(&g->player, g->save.pos, g->save.yaw);
#ifndef MG_CAMERA_THIRDPERSON
            /* v2 saves store the camera yaw; the camera is fixed now */
#endif
            Elevator *el = scene_elevator(g->scene);
            if (el && g->save.floor > 0) {
                elevator_place(el, (int)g->save.floor < el->floors ? (int)g->save.floor : 0);
            }
            g->save_play_base = g->save.play_seconds - (uint32_t)g->stats.play_seconds;
            g->save_rides_base = g->save.elevator_rides - (uint32_t)g->stats.rides;
        } else if (g->pending_play == 0) { /* New Game: a clean slot */
            save_delete(g->profile_name);
            g->has_save = false;
            SDL_zero(g->save);
            g->save_play_base = (uint32_t)0 - (uint32_t)g->stats.play_seconds;
            g->save_rides_base = (uint32_t)0 - (uint32_t)g->stats.rides;
        }
        g->next_save = app->time + 30.0;
        autonav_clear(&g->autonav);
        g->active_marker = 0;
        g->cam.initialized = g->td.initialized = false;
        g->fade_snap = true;
        touch_controls_reset(&g->touch);
        if (g->pending_play == 1) {
            session_start(&g->session, MODE_PARTY, NULL);
            SDL_snprintf(g->prefs.last_room, sizeof(g->prefs.last_room), "%s", g->session.room_code);
        } else if (g->pending_play == 2) {
            session_start(&g->session, MODE_PARTY, g->fe.code);
            SDL_snprintf(g->prefs.last_room, sizeof(g->prefs.last_room), "%s", g->fe.code);
        } else {
            session_stop(&g->session); /* offline: nothing leaves the device */
        }
        g->playing = true;
        g->fe.depth = 0;
        g->fe.state = FE_PLAY;
        save_prefs(g);
        break;
    }
}

static void start_loading(Game *g, int what)
{
    g->pending_play = what;
    g->load_step = 0;
    g->fe.state = FE_LOADING;
}

/* Everything outside PLAY: menu scene, screens, their actions. */
static void frontend_flow(Game *g, MgApp *app, bool back)
{
    Frontend *f = &g->fe;
    if (back && frontend_back(f) == FE_EXIT) {
        app->quit_requested = true;
    }
    if (f->state == FE_PLAY && g->play_paused) { /* creator closed: back in the game */
        g->playing = true;
        g->play_paused = false;
        return;
    }
    poll_sign_in(g, app);
    FrontendCtx ctx = {g->profile.display_name,
                       &g->profile.skin,
                       g->profile.guest || !g->profile_loaded,
                       g->prefs.auth_provider,
                       g->net && google_auth_available(g->google),
                       g->net && apple_auth_available(g->google),
                       g->google_busy,
                       g->has_save,
                       g->prefs.last_mode,
                       g->load_step / 2.0f,
                       MG_VERSION};
    FeAction a = frontend_frame(f, &g->ui, app, &ctx, &g->avatar_meshes);
    if (f->state == FE_SETTINGS) { /* the settings panel, full screen over the menu */
        g->menu.open = true;
        SettingsAccount acct = account_view(g);
        MenuAction ma = settings_menu(&g->menu, &g->ui, &g->settings, &acct, app->width_pt, app->height_pt,
                                      app->safe_left, app->safe_top, app->safe_right, app->safe_bottom, "");
        handle_menu_action(g, app, ma);
        app_text_input(app, g->menu.editing_name);
        if (!g->menu.open && f->state == FE_SETTINGS) {
            frontend_back(f);
            save_prefs(g);
        }
        return;
    }
    if (f->clicked) {
        audio_click(g->audio);
    }
    switch (a) {
    case FE_SIGN_IN_GOOGLE:
        g->google_busy = google_auth_start(g->google, app->window);
        break;
    case FE_SIGN_IN_APPLE:
        g->google_busy = apple_auth_start(g->google, app->window);
        break;
    case FE_GUEST:
        SDL_snprintf(g->prefs.auth_provider, sizeof(g->prefs.auth_provider), "guest");
        if (SDL_strcmp(g->profile.display_name, "Player") != 0) {
            SDL_snprintf(f->name, sizeof(f->name), "%s", g->profile.display_name);
        } else {
            SDL_snprintf(f->name, sizeof(f->name), "Player%04d", (int)SDL_rand(10000));
        }
        frontend_go(f, FE_USERNAME);
        break;
    case FE_NAME_DONE:
        SDL_snprintf(g->profile.display_name, sizeof(g->profile.display_name), "%s", f->name);
        profile_changed(g);
        frontend_go(f, FE_CREATOR);
        break;
    case FE_SKIN_CHANGED:
        profile_changed(g);
        break;
    case FE_CREATOR_DONE:
        g->prefs.onboarding_done = true;
        profile_changed(g);
        if (f->depth > 0 && (f->stack[f->depth - 1] == FE_SETTINGS || f->stack[f->depth - 1] == FE_PLAY ||
                             f->stack[f->depth - 1] == FE_MAIN_MENU)) {
            frontend_back(f); /* back where the creator was opened from */
        } else {
            f->depth = 0;
            f->state = FE_MAIN_MENU;
        }
        break;
    case FE_NEW_GAME:
        start_loading(g, 0);
        break;
    case FE_CONTINUE:
        start_loading(g, 3);
        break;
    case FE_ROOM_CREATE:
        g->prefs.last_mode = mode_find("free_roam");
        start_loading(g, 1);
        break;
    case FE_ROOM_JOIN:
        g->prefs.last_mode = mode_find("free_roam");
        start_loading(g, 2);
        break;
    case FE_LOAD_STEP:
        load_step(g, app);
        break;
    case FE_EXIT:
        app->quit_requested = true;
        break;
    default:
        break;
    }
    app_text_input(app, f->editing_name);
}

typedef struct CrowdCull {
    const npc_hot_t *hot;
    uint8_t *out;     /* per NPC: 255 not drawn, else its LOD */
    HMM_Mat4 view_proj;
    float px, pz, near2;
    int floor;
    bool lod0_only; /* --no-lod */
} CrowdCull;

/* storey + frustum + LOD, on the job system: reads the crowd, writes `out` only */
static void crowd_cull_job(void *ctx, int first, int last, int worker)
{
    (void)worker;
    const CrowdCull *c = (const CrowdCull *)ctx;
    for (int i = first; i < last; i++) {
        const npc_hot_t *h = &c->hot[i];
        c->out[i] = 255;
        if ((int)h->storey > c->floor) {
            continue;
        }
        HMM_Vec4 clip = HMM_MulM4V4(c->view_proj, HMM_V4(h->x, h->y + 1.0f, h->z, 1.0f));
        const float w = clip.W * 1.2f; /* margin: an avatar is ~1 m wide */
        if (clip.W <= 0.0f || clip.X < -w || clip.X > w || clip.Y < -w || clip.Y > w) {
            continue;
        }
        const float dx = h->x - c->px, dz = h->z - c->pz;
        const bool on_view = (h->storey < 0 ? 0 : (int)h->storey) == c->floor;
        c->out[i] = c->lod0_only ? 0 : (!on_view ? 2 : (dx * dx + dz * dz <= c->near2 ? 0 : 1));
    }
}

/* the stress dummies: every storey up to the view floor, like remote players */
static void draw_crowd(Game *g, MgApp *app, int floor, float alpha)
{
    NpcPool *p = &g->crowd.pool;
    const npc_hot_t *hot = p->hot[p->read];
    const float dt = mg_minf(app->frame_dt, 0.1f);
    const float now = (float)g->crowd.tick + alpha;
    const float near_m = g->thermal >= 2 ? 11.0f : 22.0f; /* hot device: full detail only close by */
    uint8_t *lods = ARENA_PUSH(&app->frame, uint8_t, (size_t)p->count);
    if (!lods) {
        return;
    }
    CrowdCull cull = {hot, lods, g->view_proj, g->player.kin.pos.X, g->player.kin.pos.Z, near_m * near_m, floor, g->no_lod};
    {
        PROF_SCOPE("npc.cull");
        jobs_run(app->jobs, crowd_cull_job, &cull, p->count, 64);
        jobs_wait(app->jobs);
    }
    uint32_t rendered = 0;
    for (int i = 0; i < p->count; i++) {
        if (lods[i] == 255) {
            continue;
        }
        const npc_hot_t *h = &hot[i];
        /* a slow NPC glides between its updates */
        float w = mg_clampf((now - (float)h->last_tick - 1.0f) / (float)h->rate, 0.0f, 1.0f);
        HMM_Vec3 feet = HMM_V3(h->prev_x + (h->x - h->prev_x) * w, h->y, h->prev_z + (h->z - h->prev_z) * w);
        AvatarPose pose = {feet, h->yaw, HMM_V3(h->vx, 0.0f, h->vz), true, false, h->storey >= 0, false, true, lods[i]};
        npc_cold_t *cold = &p->cold[i];
        avatar_animate(&cold->anim, &pose, dt);
        avatar_draw(app->gfx, &g->avatar_meshes, &cold->look, &pose, &cold->anim, NULL);
        rendered++;
    }
    g->rendered_entities += rendered;
}

/* 1 Hz: at "serious" or worse, NPC detail radius halves and MSAA goes off */
static void update_thermal(Game *g, MgApp *app)
{
    if (app->time < g->next_thermal_poll) {
        return;
    }
    g->next_thermal_poll = app->time + 1.0;
    int t = g->thermal_force >= 0 ? g->thermal_force : device_thermal_state();
    if ((t >= 2) != (g->thermal >= 2)) {
        mg_log_warn("thermal: state %d: NPC detail radius %s, MSAA %s", t, t >= 2 ? "11 m" : "22 m", t >= 2 ? "off" : "on");
        gfx_set_msaa(app->gfx, t >= 2 ? 1 : g->msaa_samples);
    }
    g->thermal = t;
}

void game_frame(MgApp *app, float alpha)
{
    Game *g = (Game *)app->game;
    GameSettings *s = &g->settings;
    if (g->crowd_ok && g->playing) { /* slider or scenario: spawn or remove dummies between frames */
        const int want = g->perf.active ? perf_run_stress_count(&g->perf) : (int)s->stress_count;
        if (want != g->crowd.pool.count) {
            stress_spawn(&g->crowd, g->nav, want);
        }
    }
    update_thermal(g, app);
    g->rendered_last = g->rendered_entities;
    g->rendered_entities = 1;
#ifdef MG_RENDER_FULL
    apply_tier(g, app);
#endif
    audio_set_muted(g->audio, !s->sound_on);
#ifdef MG_DAYCYCLE_CONTINUOUS
    g->day.follow_device = s->follow_clock;
    g->day.speed = s->time_speed;
    daycycle_update(&g->day, app->time, mg_minf(app->frame_dt, 0.25f));
    s->time_of_day = (float)g->day.t;
    g->day_state = daycycle_state(&g->day, (float)g->day.t);
    g->lamp_on = g->day_state.lamp_on;
#endif
    Elevator *probe_el = g->playing ? scene_elevator(g->scene) : NULL;
    AutotestProbe probe = {g->player.kin.pos,
                           g->player.kin.vel,
#ifdef MG_CAMERA_THIRDPERSON
                           HMM_ToDeg(g->cam.yaw),
#else
                           HMM_ToDeg(g->td.yaw),
#endif
                           g->player.kin.jump_count,
                           g->touch.sprint,
                           session_player_count(&g->session),
                           &g->ui,
                           (int)g->fe.state,
                           g->playing,
                           g->playing ? scene_floor_of(g->scene, g->player.kin.pos) : 0,
                           probe_el && elevator_busy(probe_el),
                           SDL_crc32(0, &g->profile.skin, sizeof(g->profile.skin)),
                           g->route.active && !g->route.done ? g->route.step : -1,
                           (int)g->autonav.state,
                           g->has_save,
                           g->session.room_code,
                           (int)g->stats.rides,
                           (float)g->stats.distance_m,
                           g->markers_reached,
                           g->active_marker,
                           g->touch.stick_touch >= 0,
                           g->hud_count,
                           g->hud_out,
                           g->playing && scene_indoors(g->scene, g->player.kin.pos)
                               ? scene_floor_of(g->scene, g->player.kin.pos)
                               : -1};
    AutotestOut ato;
    autotest_update(&g->autotest, app, &probe, &ato);
    {
        PerfSample ps;
        gfx_get_stats(app->gfx, &ps.gs);
        ps.sim = (uint32_t)g->crowd.pool.count + 1;
        ps.rendered = g->rendered_last;
        ps.nav_bytes = g->nav ? nav_memory_bytes(g->nav) : 0;
        ps.npc_bytes = sizeof(npc_hot_t) * 2u * NPC_MAX + sizeof(npc_cold_t) * NPC_MAX;
        perf_run_update(&g->perf, app, &probe, &ato, &ps);
    }
    /* --screenshot-at S:NAME[,S:NAME...]: timed pictures in any run */
    if (g->start_time == 0.0) {
        g->start_time = app->time;
    }
    const char *shots = arg_value(app->argc, app->argv, "--screenshot-at");
    int entry = 0;
    for (const char *p = shots; p && *p && !ato.screenshot[0] && entry < 32; entry++) {
        const char *colon = SDL_strchr(p, ':'), *comma = SDL_strchr(p, ',');
        if (!colon) {
            break;
        }
        int len = comma ? (int)(comma - colon - 1) : (int)SDL_strlen(colon + 1);
        bool due = app->time - g->start_time >= SDL_atof(p) && !(g->shots_taken & (1u << entry));
        if (due) {
            SDL_snprintf(ato.screenshot, sizeof(ato.screenshot), "%.*s", len, colon + 1);
            g->shots_taken |= 1u << entry;
        }
        p = comma ? comma + 1 : NULL;
    }
    if (ato.screenshot[0]) {
        prof_allocs_exempt(true); /* SDL_GetPrefPath; the capture itself is exempt in gfx */
        char path[640];
#if defined(SDL_PLATFORM_IOS)
        const char *dir = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
        size_t dl = dir ? SDL_strlen(dir) : 0;
        SDL_snprintf(path, sizeof(path), "%s%s%s.png", dir ? dir : ".", dl && dir[dl - 1] == '/' ? "" : "/",
                     ato.screenshot);
#else
        char *dir = SDL_GetPrefPath("docvex", "mobilegame");
        SDL_snprintf(path, sizeof(path), "%s%s.png", dir ? dir : "", ato.screenshot);
        SDL_free(dir);
#endif
        gfx_request_screenshot(app->gfx, path);
        prof_allocs_exempt(false);
    }
    if (ato.respawn_markers) {
        marker_clear(&g->markers, MARKER_TEST);
    }
    if (ato.nav_go && g->nav) {
        AutonavCtx ac = {g->player.kin.pos, probe.nav_storey, app->time, false, false, 0};
        autonav_go(&g->autonav, g->nav, &ac, ato.nav_pos, ato.nav_to, 0);
    }
    if (ato.start_route) {
        route_test_start(&g->route);
    }
    if (ato.kill) { /* like a swipe-kill: the app is backgrounded (saves), then gone */
        game_pause(app);
        _Exit(0);
    }
    if ((int)g->fe.state != g->logged_fe) { /* screen changes, for device logs */
        static const char *const names[] = {"WELCOME", "USERNAME", "CREATOR", "MAIN_MENU",
                                            "SETTINGS", "FREE_ROAM", "LOADING", "PLAY"};
        g->logged_fe = (int)g->fe.state;
        mg_log_info("frontend: %s", names[g->logged_fe]);
    }

    /* network: events, remote buffers, our state at the adaptive rate */
    {
        const KinematicState *k = &g->player.kin;
        Elevator *el = g->playing ? scene_elevator(g->scene) : NULL;
        uint8_t flags = (uint8_t)((k->grounded ? REP_GROUNDED : 0) | (k->jumping ? REP_JUMPING : 0) |
                                  (g->player.sprint ? REP_SPRINTING : 0) |
                                  (el && elevator_busy(el) ? REP_RIDING : 0));
        PROF_SCOPE("net.poll");
        session_update(&g->session, app->time, app->clock.tick, k->pos, k->vel, g->player.yaw, flags,
                       (uint8_t)(g->playing ? scene_floor_of(g->scene, k->pos) : 0));
        if (el) { /* someone riding from our landing: their doors close here too */
            for (int f = 0; f < el->floors; f++) {
                el->hold_shut[f] = false;
            }
            for (int i = 0; i < SESSION_MAX_REMOTE; i++) {
                const RemotePlayer *r = &g->session.remotes[i];
                if (r->active && r->visible && (r->sample.flags & REP_RIDING) && r->sample.floor < el->floors) {
                    el->hold_shut[r->sample.floor] = true;
                }
            }
        }
        if (g->session.spawn_pending && g->playing) {
            g->session.spawn_pending = false;
            int slot = g->session.slot < 0 ? 0 : g->session.slot;
            HMM_Vec3 at = scene_spawn(g->scene, slot);
            perf_run_spawn(&g->perf, &at);
            parse_vec3_arg(app->argc, app->argv, "--spawn", &at); /* desktop tests: --spawn wins */
            player_init(&g->player, at, scene_spawn_yaw(g->scene));
            g->cam.initialized = g->td.initialized = false;
            g->fade_snap = true;
        }
        if (g->session.profile_new) { /* account loaded or switched */
            g->session.profile_new = false;
            g->profile = g->session.profile;
            g->profile_loaded = true;
            g->google_busy = false;
            if (!g->profile.guest && SDL_strcmp(g->profile.display_name, "Player") == 0 && g->profile.google_name[0]) {
                /* first link: take the first name from the Google account (2-16 safe chars) */
                char name[NET_NAME_LEN];
                int n = 0;
                for (const char *c = g->profile.google_name; *c && *c != ' ' && n < 16; c++) {
                    if (SDL_isalnum((unsigned char)*c) || *c == '_' || *c == '-' || *c == '.') {
                        name[n++] = *c;
                    }
                }
                name[n] = 0;
                if (n >= 2) {
                    SDL_snprintf(g->profile.display_name, sizeof(g->profile.display_name), "%s", name);
                    net_save_profile(g->net, g->profile.display_name, &g->profile.skin);
                }
            }
            if (g->skin_override) {
                g->profile.skin = g->override_skin;
            }
            if (g->prefs.profile_stale && g->prefs.has_profile_cache) {
                /* edited while offline: ours wins over the server copy */
                SDL_snprintf(g->profile.display_name, sizeof(g->profile.display_name), "%s", g->prefs.cache_name);
                g->profile.skin = g->prefs.cache_skin;
                net_save_profile(g->net, g->profile.display_name, &g->profile.skin);
                g->prefs.profile_stale = false;
            }
            if (g->prefs.has_stats_queue) { /* offline play from earlier: add it now */
                NetStats *q = &g->prefs.stats_queue, *b = &g->profile.stats;
                b->sessions += q->sessions;
                b->play_seconds += q->play_seconds;
                b->distance_m += q->distance_m;
                b->elevator_rides += q->elevator_rides;
                b->rooms_joined += q->rooms_joined;
                SDL_zero(*q);
                g->prefs.has_stats_queue = false;
                if (g->stats_queued) { /* this run's part is counted in stats_totals already */
                    b->sessions -= g->queued.sessions;
                    b->play_seconds -= g->queued.play_seconds;
                    b->distance_m -= g->queued.distance_m;
                    b->elevator_rides -= g->queued.elevator_rides;
                    b->rooms_joined -= g->queued.rooms_joined;
                }
                g->stats_queued = false;
            }
            stats_loaded(&g->stats, &g->profile.stats, app->time);
            session_set_identity(&g->session, g->profile.display_name, g->profile.skin);
            account_loaded(g, app);
        }
        g->stats.rooms = g->session.rooms_joined;
        if (stats_flush_due(&g->stats, app->time)) {
            NetStats t = stats_totals(&g->stats);
            net_save_stats(g->net, &t);
        }
        g->local_look = session_my_look(&g->session);
        SDL_snprintf(g->status, sizeof(g->status), "%s", g->session.status_text);
    }
    if (app->input.key_pressed[SDL_SCANCODE_F1]) {
        g->fly_mode = !g->fly_mode;
    }
    if (app->input.key_pressed[SDL_SCANCODE_F2]) {
        s->show_colliders = !s->show_colliders;
    }

    /* UI claims touches first (gear + menu), then gameplay controls. The
     * menu is drawn first too; while it is open (modal) nothing else 2D is. */
    ui_begin(&g->ui, app->gfx, &app->font, &app->input);
    bool back = app->input.key_pressed[SDL_SCANCODE_AC_BACK] || app->input.key_pressed[SDL_SCANCODE_ESCAPE];
    if (!g->playing) {
        frontend_flow(g, app, back);
        return;
    }
    if (back) { /* in game: the back key opens / closes the settings panel */
        if (!g->menu.open) {
            g->menu.open = true;
            g->menu.card = true;
            g->menu.from_card = false;
        } else if (!g->menu.card && g->menu.from_card) {
            g->menu.card = true;
            g->menu.from_card = false;
        } else {
            g->menu.open = g->menu.card = false;
        }
    }
    SettingsAccount acct = account_view(g);
    g->menu.in_room = g->session.mode != MODE_OFFLINE;
    MenuAction action = settings_menu(&g->menu, &g->ui, s, &acct, app->width_pt, app->height_pt,
                                      app->safe_left, app->safe_top, app->safe_right, app->safe_bottom, g->status);
    handle_menu_action(g, app, action);
    poll_sign_in(g, app);
    app_text_input(app, g->menu.open && g->menu.editing_name);
    if (g->offline_play && app->time >= g->next_save) { /* autosave */
        g->next_save = app->time + 30.0;
        write_save(g);
    }
    if (g->menu.preview_visible) { /* the avatar turning slowly in the Skin tab */
        GfxSceneParams pc = {0};
        pc.eye = HMM_V3(0.0f, 1.35f, -3.7f);
        pc.view = HMM_LookAt_RH(pc.eye, HMM_V3(0.0f, 1.0f, 0.0f), HMM_V3(0, 1, 0));
        pc.proj = camera_projection(HMM_AngleDeg(34.0f), 1.0f, 0.05f, 50.0f, 0.0f);
        pc.stars = 0.0f;
        gfx_preview(app->gfx, &pc, g->menu.preview_x, g->menu.preview_y, g->menu.preview_size, 14.0f,
                    HMM_V4(0.16f, 0.19f, 0.26f, 1.0f));
        AvatarPose pv = {HMM_V3(0, 0, 0), (float)app->time * 0.6f, HMM_V3(0, 0, 0), true, false, false, true};
        avatar_animate(&g->preview_anim, &pv, mg_minf(app->frame_dt, 0.1f));
        AvatarLook look = avatar_look_from_skin(g->profile.skin);
        avatar_draw(app->gfx, &g->avatar_meshes, &look, &pv, &g->preview_anim, NULL);
    }
    Elevator *el = scene_elevator(g->scene);
    const bool riding = el && elevator_busy(el);
    bool menu_shown = false;
    /* auto-nav rides on its own: no floor menu unless the player cancels */
    if (el && !riding && !g->menu.open && !autonav_active(&g->autonav) &&
        elevator_contains(el, g->player.kin.pos, g->kparams.radius)) {
        menu_shown = true;
        int pick = floor_menu(&g->ui, app->width_pt, app->safe_top, app->safe_right, el->floors, el->car_floor,
                              scene_floor_name);
        if (pick >= 0) {
            elevator_request(el, pick);
        }
    }
    gfx_set_render_scale(app->gfx, s->render_scale);
#ifdef MG_CAMERA_THIRDPERSON
    g->cam.fov_y = HMM_AngleDeg(s->fov_deg);
#else
#endif
    if (g->menu.open || riding) {
        touch_controls_reset(&g->touch);
    } else {
        /* iPad: bigger controls so thumbs reach them */
        float k = mg_clampf(app->height_pt / 390.0f, 1.0f, 1.4f);
        g->touch_cfg.radius_pt = 120.0f * k;
        g->touch_cfg.dead_zone_pt = s->dead_zone_pt * k;
        g->touch_cfg.jump_size_pt = PLAYER_JUMP_ENABLED ? 72.0f * k : 0.0f;
        touch_controls_update(&g->touch, &g->touch_cfg, &app->input, app->width_pt, app->height_pt, app->safe_right,
                              app->safe_bottom, app->time);
        if (g->touch.tapped_id && g->nav) {
            marker_tapped(g, app, g->touch.tapped_id);
        }
    }

    HMM_Vec3 feet = player_render_pos(&g->player, alpha);
    GfxSceneParams sp = {0};
    float aspect = app->width_pt / app->height_pt;
    if (g->fly_mode) {
        player_set_input(&g->player, 0, 0, false, false);
        update_fly_camera(g, app);
        sp.eye = g->fly.pos;
        sp.view = camera_view(g->fly.pos, g->fly.yaw, g->fly.pitch);
        sp.proj = camera_projection(HMM_AngleDeg(s->fov_deg), aspect, 0.05f, 400.0f, 0.0f);
    } else {
        float rx, rz;
        bool route = route_test_update(&g->route, app->time, g->player.kin.pos, scene_floor_of(g->scene, feet), el,
                                       menu_shown, &rx, &rz);
        if (g->route.active && !g->route.done && scene_indoors(g->scene, feet) && !g->riding) {
            /* occlusion survey: thin ray from just under the ceiling (on the
             * camera ray) to the chest; slabs of faded storeys don't count */
            HMM_Vec3 chest = HMM_AddV3(feet, HMM_V3(0, 1.0f, 0));
            HMM_Vec3 eye = g->td.pivot;
            eye = HMM_SubV3(eye, HMM_MulV3F(camera_forward(g->td.yaw, g->td.pitch), g->td.distance));
            float top = scene_floor_y(g->scene, scene_floor_of(g->scene, feet)) + 3.6f;
            float k = (top - chest.Y) / mg_maxf(eye.Y - chest.Y, 0.01f);
            HMM_Vec3 start = HMM_AddV3(chest, HMM_MulV3F(HMM_SubV3(eye, chest), k));
            ColHit hit;
            g->occl_frames++;
            for (int step = 0; step < 8; step++) { /* walk through cut-away wall parts */
                ColCapsule ray = {start, start, 0.04f};
                HMM_Vec3 to = HMM_SubV3(chest, start);
                if (!col_capsule_sweep(&g->world, &ray, to, 0.0f, &hit) || hit.t >= 0.92f) {
                    break;
                }
                if (!scene_box_cut_away(g->scene, hit.box, g->fade.vis)) {
                    g->occl_hidden++;
                    g->occl_lost += !g->settings.show_xray; /* no silhouette to show them */
                    break;
                }
                start = HMM_AddV3(start, HMM_MulV3F(to, hit.t + 0.4f / mg_maxf(HMM_LenV3(to), 0.4f)));
            }
        }
        if (g->route.done && g->occl_frames) {
            mg_log_info("occlusion: player behind walls in %.0f %% of %u indoor frames, lost (no x-ray "
                        "silhouette) in %.0f %%",
                        100.0 * g->occl_hidden / g->occl_frames, g->occl_frames,
                        100.0 * g->occl_lost / g->occl_frames);
            g->occl_frames = 0;
        }
        if (g->route.done) {
            app->quit_requested = true;
        }
        if (g->nav) {
            PROF_SCOPE("nav.service");
            nav_service(g->nav, NAV_CLASS_PLAYER, NAV_BUDGET_MS, 0, 0);
        }
        if (g->nav && autonav_active(&g->autonav)) {
            if (g->menu.open) {
                autonav_cancel(&g->autonav);
            }
            AutonavCtx ac = {feet, scene_indoors(g->scene, feet) ? scene_floor_of(g->scene, feet) : -1, app->time,
                             el && elevator_contains(el, g->player.kin.pos, g->kparams.radius),
                             el && elevator_busy(el), el ? el->car_floor : 0};
            AutonavOut ao;
            PROF_SCOPE("autonav");
            autonav_update(&g->autonav, g->nav, &ac, &ao);
            g->auto_x = ao.move_x;
            g->auto_z = ao.move_z;
            if (ao.ride_to >= 0 && el) {
                elevator_request(el, ao.ride_to);
            }
        }
        if (g->nav) {
            PROF_SCOPE("markers");
            markers_update(g, app, feet);
        }
        g->touch.auto_dot = g->autonav.state == AUTONAV_WALKING;
        if (riding || g->menu.open) {
            player_set_input(&g->player, 0, 0, false, false); /* locked for the ride / behind the menu */
        } else if (route) {
            player_set_input(&g->player, rx, rz, false, false);
        } else {
            PROF_SCOPE("input");
            update_player_input(g, app);
        }
#ifdef MG_CAMERA_THIRDPERSON
        tpcam_update(&g->cam, feet, mg_minf(app->frame_dt, 0.1f));
        tpcam_collide(&g->cam, &g->world, scene_indoors(g->scene, feet) ? 2.4f : 100.0f, mg_minf(app->frame_dt, 0.1f));
        tpcam_matrices(&g->cam, aspect, &sp.view, &sp.proj, &sp.eye);
#else
        /* riding: the pivot rides with the car, so the floors cross-fade as it passes the slabs */
        float floor_y = g->riding ? feet.Y - g->ride_offset : scene_floor_y(g->scene, scene_floor_of(g->scene, feet));
        if (!g->perf.active) { /* Settings; perf runs keep the reference view */
            g->td.fov_y = HMM_AngleDeg(s->td_fov_deg);
            g->td.distance = s->td_distance;
            g->td.pitch = -HMM_AngleDeg(s->td_pitch_deg);
        }
        tdcam_update(&g->td, feet, g->player.kin.vel, g->kparams.walk_speed, floor_y, mg_minf(app->frame_dt, 0.1f));
        tdcam_matrices(&g->td, aspect, &sp.view, &sp.proj, &sp.eye);
#endif
    }
    const int player_floor = scene_floor_of(g->scene, g->fly_mode ? HMM_SubV3(sp.eye, HMM_V3(0, 1.5f, 0)) : feet);
    int floor = player_floor; /* the view floor */
    g->view_proj = HMM_MulM4(sp.proj, sp.view);
    scene_defaults(&sp, (float)app->time, &g->day_state);
#ifdef MG_RENDER_FULL
    sp.reflection_y = scene_floor_y(g->scene, floor);
#endif
#if !defined(MG_CAMERA_THIRDPERSON) && defined(MG_RENDER_FULL)
    sp.shadow_focus = g->fly_mode ? 0.0f : g->td.distance; /* cascades: 8 m / 30 m past the player */
#endif
    gfx_set_scene(app->gfx, &sp);

    /* M5 floor fade: inside, storeys above the view floor and the roof
     * dissolve over 350 ms (the third-person camera and fly cam keep the
     * building whole) */
    bool inside = scene_indoors(g->scene, feet) && !g->fly_mode;
#ifdef MG_CAMERA_THIRDPERSON
    inside = false;
#endif
    scene_fade_targets(g->scene, g->fade.target, floor, inside, feet, sp.eye);
    if (!g->td.initialized || g->fade_snap) { /* spawn / teleport / load: no fade */
        floorfade_snap(&g->fade);
        g->fade_snap = false;
    } else {
        floorfade_update(&g->fade, mg_minf(app->frame_dt, 0.1f));
    }
#ifdef MG_CAMERA_THIRDPERSON
    const HMM_Vec3 focus = sp.eye;
#else
    const HMM_Vec3 focus = g->fly_mode ? sp.eye : feet;
#endif
    SceneView view = {.eye = sp.eye,
                      .focus = focus,
                      .floor = floor,
                      .time = (float)app->time,
                      .show_triggers = s->show_triggers,
#ifdef MG_RENDER_FULL
                      .show_lights = s->show_lights,
#endif
                      .lamp_on = g->lamp_on,
                      .fade = g->fade.vis,
                      .storey_tint = s->show_storeys};
#ifdef MG_RENDER_FULL
    g->shadowed_lights = scene_lights(g->scene, app->gfx, &view);
#endif
    {
        PROF_SCOPE("render.cull");
        scene_draw(g->scene, app->gfx, &view);
    }
    scenery_draw(&g->scenery, app->gfx, &sp, (float)app->time, g->day_state.cloud);
    if (g->nav) {
        const char *at = arg_value(app->argc, app->argv, "--autotest");
        if (at && SDL_strcmp(at, "determinism") == 0 && g->crowd_ok) {
            run_determinism_check(g, app);
        }
        if (at && SDL_strcmp(at, "nav") == 0) {
            run_nav_check(g, app);
        }
        /* desktop: right-click on the floor = path from the player */
        if (app->input.right_click && !g->menu.open) {
            HMM_Vec3 hit;
            if (screen_to_floor(g, app, app->input.right_click_x, app->input.right_click_y,
                                scene_floor_y(g->scene, floor), &hit)) {
                int from = scene_indoors(g->scene, feet) ? player_floor : -1;
                marker_t *old = marker_get(&g->markers, g->active_marker);
                if (old) {
                    marker_set_state(old, MARKER_IDLE);
                    g->active_marker = 0;
                }
                AutonavCtx ac = {feet, from, app->time, false, false, 0};
                int to = floor == 0 && !(nav_flags_at(g->nav, 0, hit.X, hit.Z) & NAV_OUTDOOR) ? 0 : floor;
                bool ok = autonav_go(&g->autonav, g->nav, &ac, hit, to, 0);
                mg_log_info("nav: walk to (%.1f, %.1f) floor %d: %s", (double)hit.X, (double)hit.Z, floor,
                            ok ? "ok" : "unreachable");
            }
        }
        markers_draw(&g->markers, app->gfx, g->fade.vis, floor, (float)app->time, g->avatar_meshes.shadow);
        marker_t *am = marker_get(&g->markers, g->active_marker);
        if (am && autonav_active(&g->autonav) && g->autonav.next >= 0) {
            markers_draw_path(&g->markers, app->gfx, &g->autonav.path, g->autonav.next, feet, floor, am->color_rgba, am->ring_t);
        }
        if (s->show_all_paths) { /* one refreshed per frame */
            int from = scene_indoors(g->scene, feet) ? player_floor : -1;
            marker_t *rf = &g->markers.m[g->all_paths_next];
            g->all_paths_next = (g->all_paths_next + 1) % MARKER_MAX;
            if (rf->id) {
                nav_find_path(g->nav, feet, from, rf->pos, rf->storey, &g->all_paths[rf - g->markers.m]);
            }
            for (int i = 0; i < MARKER_MAX; i++) {
                if (g->markers.m[i].id) {
                    markers_draw_path(&g->markers, app->gfx, &g->all_paths[i], 1, feet, floor, g->markers.m[i].color_rgba, 1.0f);
                }
            }
        }
        if (s->show_nav) {
            nav_debug_draw(&g->nav_debug, app->gfx, &app->perm, g->nav, floor, g->unit_box,
                           autonav_active(&g->autonav) ? &g->autonav.path : (g->nav_path_ok ? &g->nav_path : NULL), true);
        }
    }
    AvatarPose pose = {feet, player_render_yaw(&g->player, alpha), g->player.kin.vel, g->player.kin.grounded,
                       g->player.kin.jumping, scene_indoors(g->scene, feet), false};
    avatar_animate(&g->local_anim, &pose, mg_minf(app->frame_dt, 0.1f));
#ifdef MG_CAMERA_THIRDPERSON
    avatar_draw(app->gfx, &g->avatar_meshes, &g->local_look, &pose, &g->local_anim, &g->world);
#else
    avatar_draw(app->gfx, &g->avatar_meshes, &g->local_look, &pose, &g->local_anim, &g->world);
    if (s->show_xray) {
        draw_xray(g, app, &g->local_look, &pose, &g->local_anim, s->xray_always);
    }
#endif
    for (int i = 0; i < SESSION_MAX_REMOTE; i++) {
        RemotePlayer *r = &g->session.remotes[i];
#ifdef MG_CAMERA_THIRDPERSON
        bool hidden_storey = (int)r->sample.floor != floor;
#else
        bool hidden_storey = (int)r->sample.floor > floor; /* below the view floor: seen through openings */
#endif
        if (!r->active || !r->visible || hidden_storey) {
            continue; /* culled by storey, not distance */
        }
        if (g->log_remote && app->time >= g->next_remote_log) { /* --log-remote: what a peer really sends */
            mg_log_info("remote: pos %.1f %.1f %.1f speed %.2f m/s sprint bit %d floor %d", (double)r->pos.X, (double)r->pos.Y,
                        (double)r->pos.Z, (double)HMM_LenV3(r->sample.vel), (r->sample.flags & REP_SPRINTING) != 0,
                        (int)r->sample.floor);
            g->next_remote_log = app->time + 1.0;
        }
        AvatarPose rp = {r->pos, r->yaw, r->sample.vel, (r->sample.flags & REP_GROUNDED) != 0,
                         (r->sample.flags & REP_JUMPING) != 0, scene_indoors(g->scene, r->pos), false};
        avatar_animate(&r->anim, &rp, mg_minf(app->frame_dt, 0.1f));
        avatar_draw(app->gfx, &g->avatar_meshes, &r->look, &rp, &r->anim, &g->world);
#ifndef MG_CAMERA_THIRDPERSON
        if (s->show_xray && (int)r->sample.floor == floor) { /* only players on the view floor */
            draw_xray(g, app, &r->look, &rp, &r->anim, s->xray_always);
        }
#endif
    }
    if (g->crowd_ok && g->crowd.pool.count > 0) {
        PROF_SCOPE("npc.draw");
        draw_crowd(g, app, floor, alpha);
    }
    if (s->show_colliders) {
        draw_colliders(g, app->gfx, feet);
    }

    if (g->menu.open) {
        return;
    }
    if (g->nav) {
        markers_hud(g, app, floor, feet, menu_shown);
    }
    touch_controls_draw(&g->touch, &g->touch_cfg, app->gfx, &app->font, app->safe_left, app->height_pt,
                        app->safe_bottom);
    frontend_toast_draw(&g->fe, &g->ui, app);
    draw_name_tags(g, app, floor, player_floor, feet);
    HudInfo hud = {0};
    hud.fps = app->fps;
    hud.frame_ms = app->frame_ms;
    hud.cpu_ms = app->cpu_ms;
    hud.pos = g->player.kin.pos;
    hud.grounded = g->player.kin.grounded;
    char mode_buf[32];
    hud.ping_ms = g->session.mode == MODE_OFFLINE ? -1 : g->session.rtt_ms;
    hud.players = session_player_count(&g->session);
    hud.mode = session_mode_label(&g->session, mode_buf, sizeof(mode_buf));
    hud.net = g->session.mode == MODE_OFFLINE ? "" : g->session.status_text;
    GfxStats gs;
    gfx_get_stats(app->gfx, &gs);
    hud.draws = gs.draws;
    hud.main_ms = gs.main_cpu_ms;
    hud.render_w = gs.width_px;
    hud.render_h = gs.height_px;
    hud_draw(app->gfx, &app->font, app->safe_left + 10.0f, app->safe_top + 8.0f, &hud);
    if (s->show_profiler != g->prof_on && !g->perf.active) {
        g->prof_on = s->show_profiler;
        prof_set_enabled(g->prof_on);
        gfx_set_gpu_timing(app->gfx, g->prof_on);
    }
    if (s->show_profiler) {
        ProfHudInfo ph = {gs.draws, gs.tris, gs.instances, gs.pipe_binds, gs.tex_binds, gs.uniform_bytes, gs.passes,
                          (uint32_t)g->crowd.pool.count + 1, g->rendered_last, gs.gpu_main_ms, gs.gpu_ui_ms,
                          device_thermal_state(), device_resident_mb()};
        prof_hud_draw(app->gfx, &app->font, app->width_pt - app->safe_right - 330.0f, app->safe_top + 56.0f, &ph);
    }
#ifdef MG_RENDER_FULL
    if (s->show_lights) {
        char line[96];
        SDL_snprintf(line, sizeof(line), "%s tier  lights %u (shadowed %d)  shadow %.2f ms / %u views  main %.2f ms (CPU)",
                     g->tier == 2 ? "high" : "low", gs.lights, gs.shadowed_lights, (double)gs.shadow_cpu_ms,
                     gs.shadow_views, (double)gs.main_cpu_ms);
        text_draw_shadow(app->gfx, &app->font, app->safe_left + 10.0f, app->safe_top + 150.0f, 14.0f,
                         ui_rgba(0xFFFFFFFF), line);
    }
#endif
    draw_room_badge(g, app);
}

/* Window point -> the horizontal plane at height y. */
static bool screen_to_floor(const Game *g, const MgApp *app, float x_pt, float y_pt, float y, HMM_Vec3 *out)
{
    HMM_Mat4 inv = HMM_InvGeneralM4(g->view_proj);
    float nx = x_pt / app->width_pt * 2.0f - 1.0f, ny = 1.0f - y_pt / app->height_pt * 2.0f;
    HMM_Vec4 a = HMM_MulM4V4(inv, HMM_V4(nx, ny, 0.0f, 1.0f)), b = HMM_MulM4V4(inv, HMM_V4(nx, ny, 1.0f, 1.0f));
    HMM_Vec3 pa = HMM_DivV3F(a.XYZ, a.W), pb = HMM_DivV3F(b.XYZ, b.W);
    if (fabsf(pb.Y - pa.Y) < 1e-4f) {
        return false;
    }
    float t = (y - pa.Y) / (pb.Y - pa.Y);
    *out = HMM_AddV3(pa, HMM_MulV3F(HMM_SubV3(pb, pa), t));
    return t > 0.0f;
}

/* ---------------------------------------------------------------- markers */

static int level_of(int storey) { return storey < 0 ? 0 : storey; }

static void marker_cue(Game *g, AudioCue cue)
{
    if (g->settings.marker_sounds) {
        audio_cue(g->audio, cue);
    }
}

static void marker_failed(Game *g, MgApp *app, marker_t *m)
{
    marker_set_state(m, MARKER_FAILED);
    marker_cue(g, AUDIO_CUE_BUZZ);
    frontend_toast(&g->fe, str("cant_get_there"), app->time);
}

static void marker_tapped(Game *g, MgApp *app, uint32_t id)
{
    marker_t *m = marker_get(&g->markers, id);
    if (!m || m->state == MARKER_REACHED) {
        return;
    }
    if (id == g->active_marker && autonav_active(&g->autonav)) {
        autonav_cancel(&g->autonav); /* tapping the active one stops; markers_update does the feedback */
        return;
    }
    marker_t *old = marker_get(&g->markers, g->active_marker);
    if (old) { /* re-target without stopping: the old one just lets go */
        marker_set_state(old, MARKER_IDLE);
    }
    g->active_marker = 0;
    HMM_Vec3 p = g->player.kin.pos;
    AutonavCtx c = {p, scene_indoors(g->scene, p) ? scene_floor_of(g->scene, p) : -1, app->time, false, false, 0};
    if (autonav_go(&g->autonav, g->nav, &c, m->pos, m->storey, id)) {
        g->active_marker = id;
        marker_set_state(m, MARKER_TAPPED);
        marker_cue(g, AUDIO_CUE_TICK);
        if (g->settings.haptics) {
            haptic_impact(false);
        }
    } else {
        autonav_clear(&g->autonav);
        marker_failed(g, app, m);
    }
    if (g->markers.on_tapped) {
        g->markers.on_tapped(g->markers.user, id);
    }
}

static void marker_reached(Game *g, MgApp *app, marker_t *m)
{
    marker_set_state(m, MARKER_REACHED);
    marker_cue(g, AUDIO_CUE_CHIME);
    if (g->settings.haptics) {
        haptic_impact(true);
    }
    g->markers_reached++;
    if (m->id == g->active_marker) {
        g->active_marker = 0;
        autonav_clear(&g->autonav);
    }
    if (m->kind == MARKER_TEST) {
        g->marker_spawn_at = app->time + 0.5;
    }
    if (g->markers.on_reached) {
        g->markers.on_reached(g->markers.user, m->id);
    }
}

/* A random spot for a test marker: 50 % rooms, 25 % corridors, 25 % yard; never
 * in the car, against a wall, near another marker or the player. */
static bool spawn_test_marker(Game *g, HMM_Vec3 feet)
{
    static const uint32_t colours[8] = {0xFF4D4DFF, 0xFFA52EFF, 0xFFD93DFF, 0x4CD964FF,
                                        0x35C9D6FF, 0x4A8BFFFF, 0xB46BFFFF, 0xFF6BB5FF};
    int roll = SDL_rand(100), want = roll < 50 ? 0 : (roll < 75 ? 1 : 2);
    for (int tries = 0; tries < 300; tries++) {
        int storey = want == 2 ? -1 : SDL_rand(nav_grid_count(g->nav));
        const NavGrid *grid = nav_grid(g->nav, level_of(storey));
        int cx = SDL_rand(grid->d.cols), cz = SDL_rand(grid->d.rows);
        uint8_t f = grid->flags[cz * grid->d.cols + cx];
        if (!(f & NAV_WALKABLE) || (f & (NAV_NEAR_WALL | NAV_ELEVATOR | NAV_DOOR))) {
            continue;
        }
        bool outdoor = (f & NAV_OUTDOOR) != 0;
        if (outdoor != (want == 2)) {
            continue;
        }
        HMM_Vec3 p = HMM_V3(grid->d.x0 + ((float)cx + 0.5f) * NAV_CELL, grid->d.floor_y, grid->d.z0 + ((float)cz + 0.5f) * NAV_CELL);
        if (!outdoor && scene_area_kind(p.X, p.Z, storey) != want) {
            continue;
        }
        bool clear = HMM_LenSqrV3(HMM_SubV3(p, feet)) > 1.5f * 1.5f;
        for (int i = 0; i < MARKER_MAX && clear; i++) {
            const marker_t *m = &g->markers.m[i];
            clear = !m->id || level_of(m->storey) != level_of(storey) || HMM_LenSqrV3(HMM_SubV3(p, m->pos)) > 1.5f * 1.5f;
        }
        /* islands (behind the rope) are out */
        int from = scene_indoors(g->scene, feet) ? scene_floor_of(g->scene, feet) : -1;
        if (!clear || !nav_can_reach(g->nav, feet, from, p, storey)) {
            continue;
        }
        return marker_add(&g->markers, p, storey, MARKER_TEST, colours[g->markers.next_id % 8]) != 0;
    }
    return false;
}

static void markers_update(Game *g, MgApp *app, HMM_Vec3 feet)
{
    Markers *ms = &g->markers;
    if (!g->marker_args_done) { /* desktop / screenshots: --markers N, --marker x,z,storey */
        g->marker_args_done = true;
        const char *count = arg_value(app->argc, app->argv, "--markers"), *one = arg_value(app->argc, app->argv, "--marker");
        float x, z;
        int st;
        const char *at = arg_value(app->argc, app->argv, "--autotest");
        g->marker_override = count ? SDL_atoi(count) : (at && SDL_strcmp(at, "markers") == 0 ? 8 : (g->perf.active ? 0 : -1));
        if (one && SDL_sscanf(one, "%f,%f,%d", &x, &z, &st) == 3) {
            marker_add(ms, HMM_V3(x, scene_floor_y(g->scene, level_of(st)), z), st, MARKER_OBJECTIVE, 0xFF9F0AFF);
        }
    }
    markers_tick(ms, mg_minf(app->frame_dt, 0.1f));
    marker_t *a = marker_get(ms, g->active_marker);
    if (g->autonav.marker_id == 0) {
        /* a walk without a marker (desktop click, autotest) keeps its result */
    } else if (g->autonav.state == AUTONAV_ARRIVED) {
        if (a) {
            marker_reached(g, app, a);
        }
        autonav_clear(&g->autonav);
    } else if (g->autonav.state == AUTONAV_CANCELLED) {
        if (a) {
            marker_set_state(a, MARKER_IDLE);
            marker_cue(g, AUDIO_CUE_DUP);
        }
        g->active_marker = 0;
        autonav_clear(&g->autonav);
    } else if (g->autonav.state == AUTONAV_FAILED) {
        if (a) {
            marker_failed(g, app, a);
        }
        g->active_marker = 0;
        autonav_clear(&g->autonav);
    }
    /* walking onto a marker counts, navigated or not */
    int level = scene_floor_of(g->scene, feet);
    int alive_test = 0;
    for (int i = 0; i < MARKER_MAX; i++) {
        marker_t *m = &ms->m[i];
        if (!m->id || m->state == MARKER_REACHED) {
            continue;
        }
        alive_test += m->kind == MARKER_TEST;
        float dx = m->pos.X - feet.X, dz = m->pos.Z - feet.Z;
        if (level_of(m->storey) == level && dx * dx + dz * dz < 0.36f) {
            marker_reached(g, app, m);
            alive_test -= m->kind == MARKER_TEST;
        }
    }
    /* test flags override the setting without being saved into prefs */
    const int wanted = g->marker_override >= 0 ? g->marker_override
                                               : (g->settings.test_markers ? (int)g->settings.test_marker_count : 0);
    if (wanted == 0) {
        if (alive_test > 0) {
            marker_clear(ms, MARKER_TEST);
        }
    } else if (alive_test < wanted && app->time >= g->marker_spawn_at) {
        spawn_test_marker(g, feet);
    } else if (alive_test > wanted) {
        for (int i = MARKER_MAX - 1; i >= 0; i--) {
            if (ms->m[i].id && ms->m[i].kind == MARKER_TEST && ms->m[i].id != g->active_marker) {
                ms->m[i].id = 0;
                break;
            }
        }
    }
}

static void markers_hud(Game *g, MgApp *app, int view_floor, HMM_Vec3 feet, bool floor_menu_up)
{
    Markers *ms = &g->markers;
    const float dt = mg_minf(app->frame_dt, 0.1f);
    int n = 0;
    for (int i = 0; i < MARKER_MAX && n < MARKER_HUD_MAX; i++) {
        marker_t *m = &ms->m[i];
        if (!m->id) {
            continue;
        }
        float dx = m->pos.X - feet.X, dz = m->pos.Z - feet.Z;
        m->dist_timer -= dt;
        if (m->dist_timer <= 0.0f) { /* the label changes at 4 Hz */
            m->dist_timer = 0.25f;
            m->dist_shown = sqrtf(dx * dx + dz * dz);
        }
        MarkerHudIn *in = &g->hud_in[n++];
        uint32_t c = m->color_rgba;
        float scale = 1.0f, shake = 0.0f;
        UiColor tint = {(uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), 255};
        if (m->state == MARKER_TAPPED) {
            scale = 1.0f + 0.3f * sinf(mg_saturate(m->anim_t / 0.15f) * 3.14159f);
            if (m->anim_t < 0.06f) {
                tint = (UiColor){255, 255, 255, 255};
            }
        } else if (m->state == MARKER_REACHED) {
            scale = 1.0f - mg_saturate(m->anim_t / 0.3f);
        } else if (m->state == MARKER_FAILED) {
            shake = 4.0f * sinf(m->anim_t * 12.0f * 6.2831853f);
            tint = (UiColor){240, 60, 60, 255};
        }
        *in = (MarkerHudIn){m->id, HMM_AddV3(m->pos, HMM_V3(0, 1.4f, 0)), level_of(m->storey), c, m->dist_shown,
                            scale, shake, m->ring_t, tint};
    }
    /* panels the icons stay out of: gear, floor menu, toast */
    const float w = app->width_pt, h = app->height_pt;
    float res[16];
    int rc = 0;
    const float gear[4] = {w - app->safe_right - 62.0f, app->safe_top + 2.0f, 56.0f, 56.0f};
    memcpy(res + rc * 4, gear, sizeof(gear));
    rc++;
    if (floor_menu_up) {
        const float fm[4] = {w - app->safe_right - 192.0f, app->safe_top + 64.0f, 180.0f, 200.0f};
        memcpy(res + rc * 4, fm, sizeof(fm));
        rc++;
    }
    if (frontend_toast_active(&g->fe, app->time)) {
        const float t[4] = {w * 0.5f - 170.0f, h - mg_maxf(app->safe_bottom, 12.0f) - 60.0f, 340.0f, 48.0f};
        memcpy(res + rc * 4, t, sizeof(t));
        rc++;
    }
    MarkerHudView v = {g->view_proj, w, h, app->safe_left, app->safe_top, app->safe_right, app->safe_bottom, view_floor, res, rc};
    g->hud_count = marker_hud_layout(g->hud_in, n, &v, g->hud_out);
    marker_hud_draw(app->gfx, &app->font, g->hud_out, g->hud_count, view_floor);
    /* tap targets for the next frame's touch pass */
    int nt = 0;
    for (int i = 0; i < g->hud_count && nt < TOUCH_MAX_TARGETS; i++) {
        const MarkerHudOut *o = &g->hud_out[i];
        if (!o->hidden && !(o->icon && o->in->scale <= 0.01f)) {
            g->touch.targets[nt].id = o->id;
            g->touch.targets[nt].x = o->x;
            g->touch.targets[nt].y = o->y;
            nt++;
        }
    }
    g->touch.target_count = nt;
    if (g->settings.test_markers && g->settings.show_nav) { /* debug line */
        marker_t *am = marker_get(ms, g->active_marker);
        char line[128];
        int alive = 0;
        for (int i = 0; i < MARKER_MAX; i++) {
            alive += ms->m[i].id != 0;
        }
        if (am) {
            SDL_snprintf(line, sizeof(line), "markers %d " TEXT_BULLET " active #%u (%.1f m, floor %d) " TEXT_BULLET
                         " path %d pts " TEXT_BULLET " %.1f ms", alive, (unsigned)am->id, (double)am->dist_shown,
                         level_of(am->storey), g->autonav.path.count, (double)nav_path_ms(g->nav));
        } else {
            SDL_snprintf(line, sizeof(line), "markers %d " TEXT_BULLET " bake %.1f ms", alive, (double)nav_bake_ms(g->nav));
        }
        text_draw_shadow(app->gfx, &app->font, app->safe_left + 10.0f, app->safe_top + 96.0f, 13.0f, ui_rgba(0xFFFFFFFF), line);
    }
}

/* --autotest nav: every room reachable from the gate, cross-storey paths go
 * through the car, the stairwell is not reachable. Logs timings, then quits. */
/* 8 requests at once through the queue, the longest ones there are */
static bool run_nav_queue_check(Game *g)
{
    static const struct { float fx, fz; int fs; float tx, tz; int ts; } r[8] = {
        {0.0f, 17.0f, -1, -10.4f, -4.0f, 0},     {20.0f, 10.0f, -1, -10.0f, -13.0f, 0},  {-20.0f, 10.0f, -1, 10.0f, -13.0f, 0},
        {0.0f, 17.0f, -1, 10.4f, -3.0f, 2},      {-10.4f, -3.0f, 2, 20.0f, 10.0f, -1},   {20.0f, 10.0f, -1, -10.4f, -3.0f, 2},
        {0.0f, 17.0f, -1, 10.4f, -4.0f, 0},      {10.0f, -12.4f, 2, -20.0f, 10.0f, -1},
    };
    nav_cache_clear(g->nav, NAV_CLASS_PLAYER);
    nav_stats_reset(g->nav);
    uint32_t id[8];
    bool ok = true;
    for (int i = 0; i < 8; i++) {
        HMM_Vec3 a = HMM_V3(r[i].fx, scene_floor_y(g->scene, r[i].fs < 0 ? 0 : r[i].fs), r[i].fz);
        HMM_Vec3 b = HMM_V3(r[i].tx, scene_floor_y(g->scene, r[i].ts < 0 ? 0 : r[i].ts), r[i].tz);
        id[i] = nav_request(g->nav, NAV_CLASS_PLAYER, a, r[i].fs, b, r[i].ts);
    }
    int frames = 0, left = 8, partials = 0;
    while (left > 0 && frames < 600) {
        nav_service(g->nav, NAV_CLASS_PLAYER, NAV_BUDGET_MS, 0, 0);
        frames++;
        for (int i = 0; i < 8; i++) {
            if (id[i]) {
                bool partial;
                NavStatus st = nav_poll(g->nav, NAV_CLASS_PLAYER, id[i], &g->nav_path, &partial);
                partials += partial;
                if (st != NAV_STATUS_PENDING) {
                    ok = ok && st == NAV_STATUS_READY;
                    id[i] = 0;
                    left--;
                }
            }
        }
    }
    NavStats st = nav_stats(g->nav);
    /* the same room pairs again: answered from the cache */
    uint32_t before = st.cache_hits;
    for (int i = 0; i < 8; i++) {
        HMM_Vec3 a = HMM_V3(r[i].fx, scene_floor_y(g->scene, r[i].fs < 0 ? 0 : r[i].fs), r[i].fz);
        HMM_Vec3 b = HMM_V3(r[i].tx, scene_floor_y(g->scene, r[i].ts < 0 ? 0 : r[i].ts), r[i].tz);
        uint32_t q = nav_request(g->nav, NAV_CLASS_PLAYER, a, r[i].fs, b, r[i].ts);
        nav_service(g->nav, NAV_CLASS_PLAYER, NAV_BUDGET_MS, 0, 0);
        for (int f = 0; f < 600 && nav_poll(g->nav, NAV_CLASS_PLAYER, q, &g->nav_path, NULL) == NAV_STATUS_PENDING; f++) {
            nav_service(g->nav, NAV_CLASS_PLAYER, NAV_BUDGET_MS, 0, 0);
        }
    }
    uint32_t hits = nav_stats(g->nav).cache_hits - before;
    /* a slice checks the clock every 24 cells: allow it a little over */
    ok = ok && left == 0 && st.worst_frame_ms <= NAV_BUDGET_MS * 1.25f && hits > 0;
    mg_log_info("nav queue: 8 requests in %d frames, longest path %.2f ms, worst frame %.2f ms (budget %.1f), %d partial, "
                "cache %u/8 on the second round: %s",
                frames, (double)st.longest_ms, (double)st.worst_frame_ms, (double)NAV_BUDGET_MS, partials, hits, ok ? "ok" : "FAIL");
    return ok;
}

/* --autotest determinism: 400 dummies twice, one thread then all of them; every
 * position must match. */
static void run_determinism_check(Game *g, MgApp *app)
{
    enum { SAMPLES = 6, TICKS_PER_SAMPLE = 300 };
    uint32_t hash[2][SAMPLES];
    const StressView view = {0.0f, -3.0f, 0};
    int threads[2] = {1, JOBS_MAX_THREADS};
    int used[2];
    double ms[2];
    for (int run = 0; run < 2; run++) {
        jobs_limit_threads(app->jobs, threads[run]);
        used[run] = jobs_thread_count(app->jobs);
        stress_reset(&g->crowd, g->nav);
        stress_spawn(&g->crowd, g->nav, STRESS_MAX);
        double t0 = mg_time_now();
        for (int s = 0; s < SAMPLES; s++) {
            for (int t = 0; t < TICKS_PER_SAMPLE; t++) {
                size_t mark = arena_mark(&app->frame);
                stress_tick(&g->crowd, g->nav, app->jobs, &app->frame, &view, MG_TICK_DT);
                arena_reset_to(&app->frame, mark);
            }
            hash[run][s] = stress_hash(&g->crowd);
        }
        ms[run] = (mg_time_now() - t0) * 1000.0 / (double)(SAMPLES * TICKS_PER_SAMPLE);
    }
    bool same = true;
    for (int s = 0; s < SAMPLES; s++) {
        same = same && hash[0][s] == hash[1][s];
        mg_log_info("determinism: tick %4d: %08x %08x %s", (s + 1) * TICKS_PER_SAMPLE, hash[0][s], hash[1][s],
                    hash[0][s] == hash[1][s] ? "same" : "DIFFERENT");
    }
    mg_log_info("determinism: %d NPCs, %d thread(s) %.3f ms per tick, %d thread(s) %.3f ms per tick", g->crowd.pool.count, used[0],
                ms[0], used[1], ms[1]);
    mg_log_info("AUTOTEST %s", same && used[1] > 1 ? "PASSED" : "FAILED");
    stress_reset(&g->crowd, g->nav);
    app->quit_requested = true;
}

static void run_nav_check(Game *g, MgApp *app)
{
    static const struct { const char *name; float x, z; int storey; bool reachable; } k[] = {
        {"lobby", -3.0f, -3.0f, 0, true},       {"pharmacy", -10.4f, -4.0f, 0, true}, {"exam 1", -10.0f, -13.0f, 0, true},
        {"triage", 10.4f, -4.0f, 0, true},      {"exam 2", 10.0f, -13.0f, 0, true},   {"car G", 0.0f, -13.84f, 0, true},
        {"yard W", -20.0f, 10.0f, -1, true},    {"yard E", 20.0f, 10.0f, -1, true},   {"room 102", -3.0f, -5.5f, 1, true},
        {"ward N", 10.0f, -12.0f, 1, true},     {"car 1", 0.0f, -13.84f, 1, true},    {"OR 1", -10.4f, -3.0f, 2, true},
        {"OR 2", 10.4f, -3.0f, 2, true},        {"office 2", 10.0f, -12.4f, 2, true}, {"cafeteria", 0.0f, -3.5f, 2, true},
        {"car 2", 0.0f, -13.84f, 2, true},      {"stairs G", -5.0f, -14.0f, 0, false},
    };
    bool all = true;
    float worst = 0.0f;
    const HMM_Vec3 gate = HMM_V3(0.0f, 0.0f, 17.0f);
    for (size_t i = 0; i < MG_ARRAY_COUNT(k); i++) {
        HMM_Vec3 to = HMM_V3(k[i].x, scene_floor_y(g->scene, k[i].storey < 0 ? 0 : k[i].storey), k[i].z);
        int r = nav_find_path(g->nav, gate, -1, to, k[i].storey, &g->nav_path);
        bool ok = (r == 0) == k[i].reachable && (!k[i].reachable || (g->nav_path.ride_at >= 0) == (k[i].storey > 0));
        worst = mg_maxf(worst, nav_path_ms(g->nav));
        all = all && ok;
        mg_log_info("nav check %-10s %s, %2d pts, %5.1f m, ride %d, %.2f ms (%s)", k[i].name, r == 0 ? "reachable " : "UNREACHABLE",
                    g->nav_path.count, (double)g->nav_path.length, g->nav_path.ride_at >= 0, (double)nav_path_ms(g->nav),
                    ok ? "ok" : "FAIL");
    }
    /* floor 2 -> yard: the reverse route */
    int r = nav_find_path(g->nav, HMM_V3(10.4f, 8.0f, -3.0f), 2, HMM_V3(-20.0f, 0.0f, 10.0f), -1, &g->nav_path);
    bool rev = r == 0 && g->nav_path.ride_at >= 0 && g->nav_path.ride_to == 0;
    worst = mg_maxf(worst, nav_path_ms(g->nav));
    all = all && rev;
    mg_log_info("nav check floor 2 -> yard %s, ride %d -> %d, %.1f m", rev ? "ok" : "FAIL", g->nav_path.ride_from,
                g->nav_path.ride_to, (double)g->nav_path.length);
    g->nav_path_ok = false;
    mg_log_info("nav: bake %.1f ms, slowest path %.2f ms", (double)nav_bake_ms(g->nav), (double)worst);
    all = run_nav_queue_check(g) && all;
    mg_log_info("AUTOTEST %s", all ? "PASSED" : "FAILED");
    app->quit_requested = true;
}

static void name_tag(Game *g, MgApp *app, HMM_Vec3 head, const char *name)
{
    HMM_Vec4 c = HMM_MulM4V4(g->view_proj, HMM_V4V(head, 1.0f));
    if (c.W <= 0.01f || !name[0]) {
        return;
    }
    float x = (c.X / c.W * 0.5f + 0.5f) * app->width_pt;
    float y = (0.5f - c.Y / c.W * 0.5f) * app->height_pt;
    const UiColor ol = {0, 0, 0, 150};
    for (int i = 0; i < 4; i++) { /* 1 pt outline */
        float dx = i == 0 ? -1.0f : (i == 1 ? 1.0f : 0.0f), dy = i == 2 ? -1.0f : (i == 3 ? 1.0f : 0.0f);
        text_draw_centered(app->gfx, &app->font, x + dx, y + dy, 12.0f, ol, name);
    }
    text_draw_centered(app->gfx, &app->font, x, y, 12.0f, (UiColor){255, 255, 255, 153}, name);
}

/* Name tags 0.5 m over the head, only for avatars on the view floor. */
static void draw_name_tags(Game *g, MgApp *app, int view_floor, int player_floor, HMM_Vec3 feet)
{
#ifdef MG_CAMERA_THIRDPERSON
    (void)g, (void)app, (void)view_floor, (void)player_floor, (void)feet;
#else
    const float head = 2.2f;
    if (g->settings.show_own_name && player_floor == view_floor) {
        name_tag(g, app, HMM_AddV3(feet, HMM_V3(0, head, 0)), g->profile.display_name);
    }
    for (int i = 0; i < SESSION_MAX_REMOTE; i++) {
        const RemotePlayer *r = &g->session.remotes[i];
        if (r->active && r->visible && (int)r->sample.floor == view_floor) {
            name_tag(g, app, HMM_AddV3(r->pos, HMM_V3(0, head, 0)), r->meta.name);
        }
    }
#endif
}

void game_event(MgApp *app, const SDL_Event *e)
{
    (void)app;
    (void)e;
}

/* Online: PATCH the totals. Never reached the server this run: keep what
 * happened in prefs.json and add it on the next successful load. */
static void flush_stats(Game *g)
{
    if (g->stats.loaded) {
        NetStats t = stats_totals(&g->stats);
        net_save_stats(g->net, &t);
        return;
    }
    if (g->stats.play_seconds < 1.0) {
        return;
    }
    NetStats d = {0};
    d = stats_totals(&g->stats); /* base is zero offline: this is the delta */
    NetStats *q = &g->prefs.stats_queue;
    q->sessions += g->prefs.has_stats_queue && g->stats_queued ? 0 : d.sessions;
    q->play_seconds += d.play_seconds - g->queued.play_seconds;
    q->distance_m += d.distance_m - g->queued.distance_m;
    q->elevator_rides += d.elevator_rides - g->queued.elevator_rides;
    q->rooms_joined += d.rooms_joined - g->queued.rooms_joined;
    g->queued = d;
    g->stats_queued = true;
    g->prefs.has_stats_queue = true;
    save_prefs(g);
}

void game_pause(MgApp *app)
{
    Game *g = (Game *)app->game;
    flush_stats(g);
    write_save(g);
    save_prefs(g);
    touch_controls_reset(&g->touch);
    player_set_input(&g->player, 0, 0, false, false);
    session_say_goodbye(&g->session, app->clock.tick, g->player.kin.pos, g->player.yaw);
    net_set_background(g->net, true); /* leave the room: others see us go at once */
}

void game_resume(MgApp *app)
{
    Game *g = (Game *)app->game;
    net_set_background(g->net, false);
}

void game_quit(MgApp *app)
{
    Game *g = (Game *)app->game;
    if (g) {
        write_save(g);
        save_prefs(g);
    }
    if (g && g->net) {
        flush_stats(g); /* the network thread finishes pending writes before it exits */
        session_say_goodbye(&g->session, app->clock.tick, g->player.kin.pos, g->player.yaw);
        net_destroy(g->net); /* sends phx_leave before closing */
        g->net = NULL;
    }
    if (g) {
        audio_destroy(g->audio);
        g->audio = NULL;
    }
}
