/* Game: scene (hospital or dev room), kinematic player, landscape touch
 * controls, diagonal top-down camera (third-person with
 * MG_CAMERA=thirdperson), HUD, settings menu and multiplayer session.
 * Keyboard/mouse also work on desktop. */
#include "game.h"

#include <stdlib.h> /* _Exit: autotest save */

#include "audio/audio.h"
#include "world/daycycle.h"
#include "platform/google_auth.h"
#include "autotest.h"
#include "avatar.h"
#include "camera_td.h"
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
    Audio *audio;
    RouteTest route;
    Stats stats;
    DayCycle day;
    int tier;              /* 1 low, 2 high (applied) */
    GfxShadowConfig shadow_cfg; /* last applied */
    DayState day_state;
    float lamp_on;        /* exterior lamps, from the day cycle */
    int shadowed_lights;  /* shadow-casting lights this frame */
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
    if (!PLAYER_JUMP_ENABLED) {
        g->touch_cfg.jump_size_pt = 0.0f; /* no jump button */
    }
    touch_controls_reset(&g->touch);

    tpcam_init(&g->cam);
    tdcam_init(&g->td);
    g->fly.pos = HMM_V3(0.0f, 8.0f, 11.5f);
    g->fly.pitch = HMM_AngleDeg(-32.0f);
    g->fly_mode = parse_cam_arg(app->argc, app->argv, &g->fly);
    g->settings.show_colliders = has_flag(app->argc, app->argv, "--colliders");
    g->settings.show_lights = has_flag(app->argc, app->argv, "--lights");
    const char *tier_arg = arg_value(app->argc, app->argv, "--tier");
    g->settings.tier_override = tier_arg ? (SDL_strcmp(tier_arg, "low") == 0 ? 1 : 2) : 0;
    g->settings.shadows_on = !has_flag(app->argc, app->argv, "--no-shadows");
    g->menu.open = has_flag(app->argc, app->argv, "--menu");
    const char *menu_tab = arg_value(app->argc, app->argv, "--menu");
    g->menu.tab = menu_tab && SDL_strcmp(menu_tab, "skin") == 0 ? 1 : 0;

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

    NetConfig ncfg = {MG_SUPABASE_HOST, MG_SUPABASE_ANON_KEY, profile ? profile : "", 25, 3,
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
        parse_vec3_arg(app->argc, app->argv, "--spawn", &spawn);
        player_init(&g->player, spawn, scene_spawn_yaw(g->scene));
        g->playing = true;
        g->fe.state = FE_PLAY;
    }
    SDL_snprintf(g->status, sizeof(g->status), "offline");
    autotest_init(&g->autotest, app->argc, app->argv);
    g->logged_fe = -1;
    g->audio = audio_create(&app->perm);
    daycycle_init(&g->day);
    g->settings.follow_clock = true;
    g->settings.time_speed = 1.0f;
    const char *time_arg = arg_value(app->argc, app->argv, "--time");
    if (time_arg) { /* desktop: fixed hour for screenshots */
        g->day.follow_device = g->settings.follow_clock = false;
        g->day.t = SDL_atof(time_arg);
    }
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
    if (el && el->arrived) {
        /* the car jumped storeys behind the fade: take the rider (and the
         * camera) along, same spot inside the car */
        el->arrived = false;
        float dy = (float)(el->car_floor - el->from_floor) * el->storey;
        if (elevator_contains(el, HMM_AddV3(g->player.kin.pos, HMM_V3(0, dy, 0)), 0.0f)) {
            g->player.kin.pos.Y += dy;
            g->player.prev_pos.Y += dy;
            g->cam.pivot.Y += dy;
            g->td.pivot.Y += dy;
        }
        audio_ding(g->audio);
        g->stats.rides++;
        stats_reset_position(&g->stats);
    }
    player_tick(&g->player, &g->kparams, &g->world, MG_TICK_DT);
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

/* Touch stick / camera drag / jump button, plus WASD, Shift, Space, arrows
 * and right-mouse drag on desktop. */
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
    /* right thumb / middle or right mouse drag = yaw, pinch / wheel = zoom,
     * Q / E = 45 deg steps, double tap = recentre */
    TdCamera *td = &g->td;
    tdcam_drag(td, -HMM_AngleDeg(s->yaw_deg_per_pt) * look_x);
    if (g->touch.camera_released || (td->dragging && !in->mouse_right && g->touch.camera_touch < 0)) {
        tdcam_release(td);
    }
    if (g->touch.pinch_dpt != 0.0f) { /* ~0.02 m per pt at 16 m, logarithmic */
        tdcam_zoom(td, expf(-g->touch.pinch_dpt * 0.02f / TDCAM_DEFAULT_DISTANCE));
    }
    if (in->wheel != 0.0f) {
        tdcam_zoom(td, expf(-in->wheel * 0.1f));
    }
    if (in->key_pressed[SDL_SCANCODE_Q]) {
        tdcam_step(td, -1);
    }
    if (in->key_pressed[SDL_SCANCODE_E]) {
        tdcam_step(td, 1);
    }
    if (g->touch.double_tap) {
        tdcam_recentre(td);
    }
    g->settings.cam_distance = td->distance;
    const float cam_yaw = td->yaw;
    (void)dt;
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
    s->yaw = g->player.yaw;
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
    case MENU_TIME: /* slider: stop following the clock */
        g->day.t = g->settings.time_of_day;
        g->settings.follow_clock = false;
        break;
    case MENU_TELEPORT: {
        Elevator *el = scene_elevator(g->scene);
        int f = g->settings.teleport_floor;
        player_init(&g->player, scene_floor_lobby(g->scene, f), 0.0f);
        if (el) {
            elevator_place(el, f);
        }
        g->cam.initialized = g->td.initialized = false;
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
    sp->reflection_y = 0.0f;
    sp->reflection_strength = 1.0f;
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
        g->cam.initialized = g->td.initialized = false;
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

void game_frame(MgApp *app, float alpha)
{
    Game *g = (Game *)app->game;
    GameSettings *s = &g->settings;
    apply_tier(g, app);
    g->day.follow_device = s->follow_clock;
    g->day.speed = s->time_speed;
    daycycle_update(&g->day, app->time, mg_minf(app->frame_dt, 0.25f));
    s->time_of_day = (float)g->day.t;
    g->day_state = daycycle_state(&g->day, (float)g->day.t);
    g->lamp_on = g->day_state.lamp_on;
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
                           1,
                           &g->ui,
                           (int)g->fe.state,
                           g->playing,
                           g->playing ? scene_floor_of(g->scene, g->player.kin.pos) : 0,
                           probe_el && elevator_busy(probe_el),
                           SDL_crc32(0, &g->profile.skin, sizeof(g->profile.skin)),
                           g->route.active && !g->route.done ? g->route.step : -1};
    AutotestOut ato;
    autotest_update(&g->autotest, app, &probe, &ato);
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
            player_init(&g->player, scene_spawn(g->scene, slot), scene_spawn_yaw(g->scene));
            g->cam.initialized = g->td.initialized = false;
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
        g->menu.open = !g->menu.open;
    }
    SettingsAccount acct = account_view(g);
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
    if (el && !riding && !g->menu.open && elevator_contains(el, g->player.kin.pos, g->kparams.radius)) {
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
    g->td.distance = mg_clampf(s->cam_distance, TDCAM_MIN_DISTANCE, TDCAM_MAX_DISTANCE);
    g->td.lock_north = s->cam_yaw_lock;
    g->td.snap = s->cam_snap;
    g->td.pitch = -HMM_AngleDeg(s->cam_pitch_override);
    g->td.fov_y = HMM_AngleDeg(s->cam_fov_td);
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
        if (g->route.done) {
            app->quit_requested = true;
        }
        if (riding || g->menu.open) {
            player_set_input(&g->player, 0, 0, false, false); /* locked for the ride / behind the menu */
        } else if (route) {
            player_set_input(&g->player, rx, rz, false, false);
        } else {
            update_player_input(g, app);
        }
#ifdef MG_CAMERA_THIRDPERSON
        tpcam_update(&g->cam, feet, mg_minf(app->frame_dt, 0.1f));
        tpcam_collide(&g->cam, &g->world, scene_indoors(g->scene, feet) ? 2.4f : 100.0f, mg_minf(app->frame_dt, 0.1f));
        tpcam_matrices(&g->cam, aspect, &sp.view, &sp.proj, &sp.eye);
#else
        float floor_y = scene_floor_y(g->scene, scene_floor_of(g->scene, feet));
        tdcam_update(&g->td, feet, g->player.kin.vel, g->kparams.sprint_speed, floor_y, mg_minf(app->frame_dt, 0.1f));
        tdcam_matrices(&g->td, aspect, &sp.view, &sp.proj, &sp.eye);
#endif
    }
    int floor = scene_floor_of(g->scene, g->fly_mode ? HMM_SubV3(sp.eye, HMM_V3(0, 1.5f, 0)) : feet);
    scene_defaults(&sp, (float)app->time, &g->day_state);
    sp.reflection_y = scene_floor_y(g->scene, floor);
    gfx_set_scene(app->gfx, &sp);

    SceneView view = {sp.eye, floor, (float)app->time, s->show_triggers, s->show_lights, g->lamp_on};
    g->shadowed_lights = scene_lights(g->scene, app->gfx, &view);
    scene_draw(g->scene, app->gfx, &view);
    scenery_draw(&g->scenery, app->gfx, &sp, (float)app->time, g->day_state.cloud);
    AvatarPose pose = {feet, player_render_yaw(&g->player, alpha), g->player.kin.vel, g->player.kin.grounded,
                       g->player.kin.jumping, scene_indoors(g->scene, feet), false};
    avatar_animate(&g->local_anim, &pose, mg_minf(app->frame_dt, 0.1f));
    avatar_draw(app->gfx, &g->avatar_meshes, &g->local_look, &pose, &g->local_anim, &g->world);
    for (int i = 0; i < SESSION_MAX_REMOTE; i++) {
        RemotePlayer *r = &g->session.remotes[i];
        if (!r->active || !r->visible || (int)r->sample.floor != floor) {
            continue; /* culled by storey, not distance */
        }
        AvatarPose rp = {r->pos, r->yaw, r->sample.vel, (r->sample.flags & REP_GROUNDED) != 0,
                         (r->sample.flags & REP_JUMPING) != 0, scene_indoors(g->scene, r->pos), false};
        avatar_animate(&r->anim, &rp, mg_minf(app->frame_dt, 0.1f));
        avatar_draw(app->gfx, &g->avatar_meshes, &r->look, &rp, &r->anim, &g->world);
    }
    if (s->show_colliders) {
        draw_colliders(g, app->gfx, feet);
    }

    if (g->menu.open) {
        return;
    }
    touch_controls_draw(&g->touch, &g->touch_cfg, app->gfx, &app->font, app->safe_left, app->height_pt,
                        app->safe_bottom);
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
    hud.reflect_draws = gs.reflect_draws;
    hud.render_w = gs.width_px;
    hud.render_h = gs.height_px;
    hud_draw(app->gfx, &app->font, app->safe_left + 10.0f, app->safe_top + 8.0f, &hud);
    if (s->show_lights) {
        char line[96];
        SDL_snprintf(line, sizeof(line), "%s tier  lights %u (shadowed %d)  shadow %.2f ms / %u views  main %.2f ms (CPU)",
                     g->tier == 2 ? "high" : "low", gs.lights, gs.shadowed_lights, (double)gs.shadow_cpu_ms,
                     gs.shadow_views, (double)gs.main_cpu_ms);
        text_draw_shadow(app->gfx, &app->font, app->safe_left + 10.0f, app->safe_top + 150.0f, 14.0f,
                         ui_rgba(0xFFFFFFFF), line);
    }
    draw_room_badge(g, app);
    float fade = el ? elevator_fade(el) : 0.0f;
    if (fade > 0.0f) {
        ui_rect(app->gfx, &app->font, 0.0f, 0.0f, app->width_pt, app->height_pt,
                (UiColor){0, 0, 0, (uint8_t)(fade * 255.0f)});
    }
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
