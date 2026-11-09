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
#include "npc_sim.h"
#include "interact.h"
#include "perf_run.h"
#include "preview_panel.h"
#include "rooms.h"
#include "platform/device_stats.h"
#include "stress.h"
#include "ui/layout.h"
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
#include "elevator_panel.h"
#include "frontend.h"
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
#include "friends.h"
#include "lobby.h"
#include "friends_page.h"
#include "account_link.h"
#include "ui/head_icons.h"
#include "ui/notice_panel.h"
#ifdef MG_TEST_AUTH
#include "platform/secure_store.h"
#endif
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
    int pending_play;      /* what LOADING leads to: 0 new game, 3 continue, 4 a friend's lobby */
    char status[96];
    Autotest autotest;
    PerfRun perf;
    StressCrowd crowd;
    bool crowd_ok, prof_on, no_lod;
    RoomGraph rooms;
    Interact ia;
    PreviewPanel panel;
    ElevatorPanel lift_panel;
    bool lift_panel_ok;
    bool ia_ok, ia_log;
    MgApp *ia_app;  /* for the interact callbacks */
    uint16_t ia_any_slot;    /* Developer -> Preview any mesh */
    uint32_t npc_sync_tick, npc_sync_hash; /* every 600 ticks, for the tests */
    Friends friends;
    Lobby lobby;
    HeadIcons heads;
    FriendsPage fpage;
    NoticePanel notice;
    AccountLink link;
    bool menu_world_on;    /* the hospital and its NPCs are the menu's backdrop */
    float menu_dist, menu_eye_z;
    HMM_Vec3 menu_slots[LOBBY_MAX_PLAYERS];
    LobbyRow menu_rows[LOBBY_MAX_PLAYERS];
    int menu_row_count;
    bool lobby_visible;
    bool demo_lobby;       /* --autotest perf menu, --autotest lobbylayout */
    HMM_Vec3 menu_hero;
    float frame_alpha;
    bool ia_check;           /* --autotest interact: brute force against the scan */
    bool no_panels;          /* --no-panels: A/B for the perf audit */
    bool text_test, hud_clamp_test; /* --autotest text, hudclamp */
    float hud_res[24];       /* what the marker icons stay out of: x, y, w, h */
    int hud_res_count;
    int player_storey_cached;
    int ia_enter_run, ia_enter_lag, ia_exit_run, ia_exit_lag; /* ticks */
    uint16_t ia_hover_slot;  /* --autotest interact */
    char greet_name[32], greet_line[96];
    float greet_t;
    uint32_t greet_count;
    uint16_t marker_ia[MARKER_MAX];
    uint32_t marker_ia_id[MARKER_MAX];
    NpcWorld npcs;
    bool npcs_ok, npcs_on;
    bool npc_catching_up, npc_sync_log;
    double npc_epoch_ms;       /* online: wall clock of tick 0 */
    uint32_t npc_resume_tick;  /* offline Continue: the save's tick */
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
    bool fill_name_from_provider;
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

static void interact_setup(Game *g, MgApp *app);
static void interact_tick(Game *g, MgApp *app);
static void interact_probe(Game *g, MgApp *app, AutotestProbe *pr);
static void draw_ia_debug(Game *g, MgApp *app, int floor);
static int grid_of_storey(int storey) { return storey < 0 ? 0 : storey; }
static void interact_tap(Game *g, MgApp *app);
static void lift_panel_press(Game *g, Elevator *el, int floor);
static void marker_reached(Game *g, MgApp *app, marker_t *m);

static const Interact k_no_interact = {.active = IA_NONE}; /* the panel with nothing to show */

static bool perf_wants_npcs(const Game *g)
{
    return g->perf.active && (SDL_strcmp(g->perf.scenario, "npcs") == 0 || SDL_strcmp(g->perf.scenario, "interact_npc") == 0 || SDL_strcmp(g->perf.scenario, "menu") == 0);
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
        if (g->nav) {
            scene_rooms_build(g->scene, &g->rooms);
            int bad = rooms_validate(&g->rooms, g->nav);
            mg_log_info("rooms: %d rooms, %d doors, %d slots, %d on blocked cells", g->rooms.room_count, g->rooms.door_count,
                        g->rooms.slot_count, bad);
            g->npcs_ok = npc_world_init(&g->npcs, &app->perm, &g->rooms, g->nav);
            interact_setup(g, app);
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

/* the account chip, top right; the perf text goes under the pause button, top left */
#define STATS_Y(top, corner) ((top) + 8.0f + (corner) + UI_PAUSE_SIZE + 8.0f)
#define CHIP_RESERVED_W 170.0f
/* the main menu's view: from the gate, the hero in the right half */
#define MENU_EYE_X 0.5f
#define MENU_EYE_Y 0.35f /* low enough to see the feet and the contact disc */
#define MENU_HERO_DIST 2.53f /* the camera, from the hero; further back when the lobby needs the room */
#define MENU_SLOT_GAP 0.9f
#define MENU_LOOK_X 0.5f
#define MENU_LOOK_Y 6.8f
#define MENU_LOOK_Z 0.0f
#define MENU_FOV_DEG 64.8f
#define MENU_HERO_Z 16.2f
#define MENU_HERO_MID_Y 0.9f
#define MENU_HERO_FEET_Y 0.0f /* the body floats over its contact disc, as in play */
#define NOTICE_FRIEND_S 15.0
#define NOTICE_INFO_S 3.0

/* --autotest perf lobby4: three guests standing around and the join panel up, without a network */
static void lobby4_fake(Game *g)
{
    static const NetSkin skins[3] = {{.hair = 2, .accessory = 1}, {.hair = 4, .accessory = 2, .skin = 3}, {.hair = 1, .accessory = 4, .skin = 5}};
    for (int i = 0; i < 3; i++) {
        RemotePlayer *r = &g->session.remotes[i];
        r->active = r->visible = true;
        SDL_snprintf(r->id, sizeof(r->id), "perf%d", i);
        SDL_snprintf(r->meta.name, sizeof(r->meta.name), "Guest %d", i + 1);
        r->meta.skin = skins[i];
        r->look = avatar_look_from_skin(skins[i]);
        r->pos = HMM_V3(-1.5f + 1.5f * (float)i, 0.1f, -1.5f);
        r->sample.valid = true;
        r->sample.pos = r->pos;
    }
    notice_push(&g->notice, NOTICE_JOIN, "perf", "Guest", &skins[0], str("notice_wants_join"), 100000.0);
}

static void on_net_event(void *user, const NetEvent *e)
{
    Game *g = (Game *)user;
    friends_on_event(&g->friends, e);
    lobby_on_event(&g->lobby, e);
    account_link_on_event(&g->link, e);
    if (e->type == NET_EV_PUSH && e->push.type == NET_PUSH_FRIEND_REQUEST) {
        notice_push(&g->notice, NOTICE_FRIEND, e->push.from, e->push.name, &e->push.skin, str("notice_friend_request"), NOTICE_FRIEND_S);
    }
}

static double wall_clock_ms(void);

/* One head icon, for ui/head_icons.c: the interact preview's floating head, turned 15 degrees */
static void draw_head_icon(void *user, GfxContext *gfx, const NetSkin *skin)
{
    Game *g = (Game *)user;
    const AvatarLook look = avatar_look_from_skin(*skin);
    AvatarPose pose;
    SDL_zero(pose);
    pose.yaw = HMM_PI32 + HMM_AngleDeg(15.0f); /* avatars look along -Z: turn them to the camera */
    pose.grounded = true;
    pose.head_only = true;
    AvatarAnim anim;
    SDL_zero(anim);
    avatar_animate(&anim, &pose, 1.0f / 60.0f);
    avatar_draw(gfx, &g->avatar_meshes, &look, &pose, &anim, NULL);
}

static uint32_t head_icon_rgb(void *user, const NetSkin *skin)
{
    (void)user;
    return avatar_skin_rgb(skin->skin);
}

/* Wall clock of NPC tick 0, as the sim stands: a guest catches up from it */
static double lobby_epoch(void *user)
{
    const Game *g = (const Game *)user;
    if (g->npc_epoch_ms > 0.0) {
        return g->npc_epoch_ms;
    }
    const uint32_t tick = g->npcs_on && g->npcs.tick > g->npc_resume_tick ? g->npcs.tick : g->npc_resume_tick;
    return wall_clock_ms() - (double)tick / 0.06;
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
    const char *ft_role = NULL;
    for (int i = 1; atest && SDL_strcmp(atest, "friends") == 0 && i + 2 < app->argc; i++) {
        ft_role = SDL_strcmp(app->argv[i], "--autotest") == 0 ? app->argv[i + 2] : ft_role;
    }
    const bool no_network = atest && (SDL_strcmp(atest, "offline") == 0 || g->perf.active || (ft_role && SDL_strcmp(ft_role, "offline") == 0));
    NetConfig ncfg = {no_network ? "offline.invalid" : MG_SUPABASE_HOST, MG_SUPABASE_ANON_KEY, profile ? profile : "", 25, 3, false};
#ifdef MG_TEST_AUTH
    { /* a device has one keychain: set its session aside for a new-install run, and put it back after */
        SecureStore store;
        char token[256];
        secure_store_init(&store, profile ? profile : "");
        if (has_flag(app->argc, app->argv, "--test-fresh-guest")) {
            char kept[256];
            if (secure_store_get(&store, "sb_refresh_token", token, sizeof(token)) && !secure_store_get(&store, "sb_test_kept", kept, sizeof(kept))) {
                secure_store_set(&store, "sb_test_kept", token);
            }
            secure_store_remove(&store, "sb_refresh_token");
        } else if (has_flag(app->argc, app->argv, "--test-restore-guest") && secure_store_get(&store, "sb_test_kept", token, sizeof(token))) {
            secure_store_set(&store, "sb_refresh_token", token);
            secure_store_remove(&store, "sb_test_kept");
        }
    }
#endif
    g->net = net_create(&app->perm, &ncfg);
    session_init(&g->session, g->net);
    friends_init(&g->friends, g->net, profile ? profile : "");
    g->session.listener = on_net_event;
    g->session.listener_user = g;
    head_icons_init(&g->heads, draw_head_icon, head_icon_rgb, g);
    account_link_init(&g->link, g->net);
    lobby_init(&g->lobby, &g->session, &g->friends, g->prefs.lobby_open);
    g->lobby.epoch_ms = lobby_epoch;
    g->lobby.epoch_user = g;
    if (g->perf.active && SDL_strcmp(g->perf.scenario, "lobby4") == 0) {
        lobby4_fake(g);
    }
    g->menu.lobby = &g->lobby;
    g->menu.friends = &g->friends;
    g->menu.heads = &g->heads;
    g->menu.my_name = g->profile.display_name;
    g->menu.my_skin = &g->profile.skin;
    session_set_identity(&g->session, g->profile.display_name, g->profile.skin);
    net_account_start(g->net); /* guest sign-in + profile/stats, also for offline play */
    /* the front end comes first unless a test flag already chose to play */
    bool play_now = has_flag(app->argc, app->argv, "--offline") ||
                    autotest_starts_in_game(app->argc, app->argv) || arg_value(app->argc, app->argv, "--spawn") ||
                    arg_value(app->argc, app->argv, "--cam") || has_flag(app->argc, app->argv, "--menu") ||
                    has_flag(app->argc, app->argv, "--colliders");
    const char *fe_arg = arg_value(app->argc, app->argv, "--frontend"); /* desktop: start on a screen */
    g->fe.state = g->prefs.onboarding_done ? FE_MAIN_MENU : FE_WELCOME;
    if (fe_arg) {
        static const char *const names[] = {"welcome", "username", "creator", "menu", "settings", "friends"};
        for (int i = 0; i < 6; i++) {
            if (SDL_strcmp(fe_arg, names[i]) == 0) {
                g->fe.state = (FeState)i;
            }
        }
        if (g->fe.state == FE_FRIENDS) {
            friends_page_open(&g->fpage, &g->friends);
        }
    }
    if (g->perf.active && SDL_strcmp(g->perf.scenario, "menu") == 0) {
        play_now = false;
        g->fe.state = FE_MAIN_MENU;
        g->demo_lobby = true;
    }
    if (arg_value(app->argc, app->argv, "--autotest") && SDL_strcmp(arg_value(app->argc, app->argv, "--autotest"), "lobbylayout") == 0) {
        play_now = false;
        g->fe.state = FE_MAIN_MENU;
        g->demo_lobby = true;
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
    g->npc_sync_log = has_flag(app->argc, app->argv, "--npc-sync-log") || (atest && SDL_strcmp(atest, "friends") == 0);
    g->ia_log = has_flag(app->argc, app->argv, "--ia-log");
    if (atest && (SDL_strcmp(atest, "npcs-save") == 0 || SDL_strcmp(atest, "npcs-continue") == 0)) {
        g->offline_play = true;
        if (SDL_strcmp(atest, "npcs-continue") == 0 && save_read(&g->save, g->profile_name)) {
            g->npc_resume_tick = g->save.npc_tick;
        }
    }
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

static void npc_step(Game *g, MgApp *app);

void game_tick(MgApp *app)
{
    Game *g = (Game *)app->game;
    if (!g->playing) {
        if (g->menu_world_on && g->npcs_on && !g->settings.freeze_npcs) { /* the people outside the menu */
            npc_step(g, app);
        }
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
    if (g->npcs_ok && g->npcs_on && g->nav && !g->settings.freeze_npcs && !g->npc_catching_up) {
        PROF_SCOPE("npc.roster");
        npc_step(g, app);
    }
    interact_tick(g, app);
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
static void layout_autotest(Game *g, MgApp *app);
static void draw_npcs(Game *g, MgApp *app, int floor, float alpha);
static void npc_autotest_check(Game *g, MgApp *app);
static void name_tag(Game *g, MgApp *app, HMM_Vec3 head, const char *name);
static void run_determinism_check(Game *g, MgApp *app);
static void run_npc_bench(Game *g, MgApp *app);
static uint32_t npc_seed_for(Game *g, MgApp *app);
static void npc_step(Game *g, MgApp *app);
static void npc_catch_up(Game *g, MgApp *app);
static void npc_idle_touches(Game *g, MgApp *app, int i, const npc_hot_t *h, npc_cold_t *c, AvatarPose *pose, float dt, bool queue_waiting);
static void npc_save_test_compare(Game *g, MgApp *app);
static void npc_save_test_save(Game *g, MgApp *app);
static void markers_update(Game *g, MgApp *app, HMM_Vec3 feet);
static void marker_tapped(Game *g, MgApp *app, uint32_t id);
static void markers_hud(Game *g, MgApp *app, int view_floor, HMM_Vec3 feet);
static bool screen_to_floor(const Game *g, const MgApp *app, float x_pt, float y_pt, float y, HMM_Vec3 *out);

/* X-ray silhouette colour: the avatar's accent at full saturation x 0.85
 * (grey accents stay grey), so players can tell each other apart. */
/* M5-X: the hidden parts of an avatar - a 2 px dark rim, then the flat
 * accent fill (both dithered 60 %, depth GREATER; `always` ignores depth). */
static void draw_xray(Game *g, MgApp *app, const AvatarLook *look, const AvatarPose *pose, const AvatarAnim *anim,
                      bool always)
{
    GfxMaterial rim = mat_xray(HMM_V4(0.0f, 0.0f, 0.0f, 1.0f), true, always);
    GfxMaterial fill = mat_xray(HMM_V4(0.0f, 0.0f, 0.0f, 1.0f), false, always); /* always black, never the player's colour */
    avatar_draw_with(app->gfx, &g->avatar_meshes, look, pose, anim, &rim);
    avatar_draw_with(app->gfx, &g->avatar_meshes, look, pose, anim, &fill);
}

/* Touch stick / jump button, plus WASD, Shift, Space on desktop
 * (third-person: also camera drag, arrows and right-mouse drag). */
/* The in-game HUD runs edge to edge on every model: under the island and the home indicator. */
static ui_frame_t hud_frame(const MgApp *app) { return (ui_frame_t){app->width_pt, app->height_pt, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}; }

static ui_frame_t frame_of(const MgApp *app)
{
    return (ui_frame_t){app->width_pt, app->height_pt, app->safe_left, app->safe_right, app->safe_top, app->safe_bottom, app->corner_clearance};
}

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
    s->npc_tick = g->npcs_on ? g->npcs.tick : g->npc_resume_tick;
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

/* Back to the main menu from the hospital (leaves or closes the lobby). */
static void leave_play(Game *g, MgApp *app)
{
    (void)app;
    write_save(g);
    if (lobby_is_host(&g->lobby) && !lobby_in_menu(&g->lobby)) { /* the whole lobby comes back with us */
        lobby_return_to_menu(&g->lobby);
    } else if (!lobby_in_menu(&g->lobby)) {
        session_say_goodbye(&g->session, 0, g->player.kin.pos, g->player.yaw);
        lobby_stop(&g->lobby);
    }
    g->npc_epoch_ms = 0.0; /* the next game has its own tick 0 */
    session_set_npc_epoch(&g->session, 0.0);
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

#ifdef MG_TEST_AUTH
static void test_sign_in(Game *g);
#endif

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
#ifdef MG_TEST_AUTH
        test_sign_in(g);
        break;
#endif
        g->google_busy = google_auth_start(g->google, app->window);
        break;
    case MENU_SIGN_OUT:
        /* device settings and the save stay; the account cache does not */
        g->prefs.has_profile_cache = false;
        g->prefs.onboarding_done = false;
        g->prefs.auth_provider[0] = 0;
        save_prefs(g);
        if (!g->playing) {
            lobby_stop(&g->lobby);
        }
        net_sign_out(g->net);
        if (!g->playing) {
            g->menu.open = false;
            g->fe.depth = 0;
            g->fe.state = FE_WELCOME;
        }
        SDL_snprintf(g->account_msg, sizeof(g->account_msg), "Signed out - playing as a new guest");
        break;
    case MENU_LOBBY_TOGGLE:
        g->prefs.lobby_open = g->lobby.open;
        save_prefs(g);
        break;
    case MENU_FRIENDS:
        friends_page_open(&g->fpage, &g->friends);
        break;
    case MENU_LINK_ACCOUNT: /* never mid-session */
        leave_play(g, app);
        g->menu.tab = 1;
        frontend_go(&g->fe, FE_SETTINGS);
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

/* In company, connection problems show at the top in red; a solo lobby plays on regardless. */
static void draw_net_badge(Game *g, MgApp *app)
{
    const NetSession *s = &g->session;
    if ((!lobby_is_guest(&g->lobby) && g->lobby.state != LOBBY_HOSTING) || s->status == NET_JOINED) {
        return;
    }
    const float cx = app->width_pt * 0.5f, y = 104.0f; /* below the debug HUD lines */
    const char *msg = s->status_text;
    float mw = mg_minf(text_width(&app->font, msg, 14.0f), app->width_pt - 40.0f);
    ui_round_rect(app->gfx, &app->font, cx - mw * 0.5f - 12.0f, y, mw + 24.0f, 26.0f, 13.0f, ui_rgba(0xD94141E0));
    text_draw_centered(app->gfx, &app->font, cx, y + 13.0f, 14.0f, ui_rgba(0xFFFFFFFF), msg);
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
    g->menu.is_guest = g->profile.guest;
#ifdef MG_TEST_AUTH
    const bool test_google = true;
#else
    const bool test_google = false;
#endif
    SettingsAccount a = {g->profile.display_name,
                         g->profile.email,
                         g->profile.guest || !g->profile_loaded,
                         g->net && (test_google || google_auth_available(g->google)),
                         g->net && apple_auth_available(g->google),
                         g->prefs.auth_provider,
                         g->google_busy,
                         g->session.account_error[0] ? g->session.account_error : g->account_msg};
    return a;
}

/* Native sign-in sheet finished: hand the token to Supabase to link. */
/* What the link flow settled: the provider, and after a merge everything cached for the old identity */
#ifdef MG_TEST_AUTH
/* The sign-in buttons, without a provider sheet: $MG_LT_EMAIL stands in for the identity */
static void test_sign_in(Game *g)
{
    char cred[160];
    SDL_snprintf(cred, sizeof(cred), "%s mgtest-password-1", SDL_getenv("MG_LT_EMAIL"));
    g->google_busy = true;
    account_link_begin(&g->link, "test", cred, "");
}
#endif

static void follow_account_link(Game *g, MgApp *app)
{
    AccountLink *l = &g->link;
    if (l->linked || l->merged) {
        SDL_snprintf(g->prefs.auth_provider, sizeof(g->prefs.auth_provider), "%s", l->provider);
    }
    if (l->linked) {
        l->linked = false;
        g->fill_name_from_provider = true;
        frontend_toast(&g->fe, str("link_ok"), app->time);
        save_prefs(g);
    }
    if (l->merged) {
        l->merged = false;
        lobby_stop(&g->lobby); /* its room belonged to the guest that is gone */
        g->prefs.has_profile_cache = false; /* device data stays: settings and the save slot */
        g->stats.loaded = false;            /* this run's numbers are in the merged totals already */
        g->stats.play_seconds = g->stats.distance_m = 0.0;
        g->stats.rides = g->stats.rooms = 0;
        friends_forget(&g->friends);
        head_icons_clear(&g->heads);
        g->pending_name[0] = 0;
        save_prefs(g);
    }
    if (l->failed) {
        frontend_toast(&g->fe, str(l->failed), app->time);
        l->failed = NULL;
        g->google_busy = false;
    }
}

static void poll_sign_in(Game *g, MgApp *app)
{
    char token[4096], nonce[96], err[160], name[64];
    char provider[12];
    GoogleAuthState gs = native_auth_poll(g->google, provider, sizeof(provider), token, sizeof(token), nonce,
                                          sizeof(nonce), name, sizeof(name), err, sizeof(err));
    if (gs == GOOGLE_DONE) {
        if (name[0]) { /* Apple sends the full name only once: keep it now */
            SDL_snprintf(g->pending_name, sizeof(g->pending_name), "%s", name);
        }
        account_link_begin(&g->link, provider, token, nonce);
        SDL_memset(token, 0, sizeof(token));
    } else if (gs == GOOGLE_FAILED) {
        g->google_busy = false;
        bool cancelled = SDL_strcmp(err, "cancelled") == 0;
        SDL_snprintf(g->account_msg, sizeof(g->account_msg), "%s", cancelled ? str("cancelled") : err);
        frontend_toast(&g->fe, cancelled ? str("cancelled") : str("no_connection"), app->time);
    }
    if (g->session.account_error[0] && g->google_busy && !account_link_active(&g->link)) {
        g->google_busy = false;
        frontend_toast(&g->fe, str("no_connection"), app->time);
    }
    follow_account_link(g, app);
}

/* Account (re)loaded from Supabase: cache it on the device and route the
 * welcome screen (a linked account with a name skips onboarding). */
static void account_loaded(Game *g, MgApp *app)
{
    (void)app;
    if (g->fill_name_from_provider && !g->pending_name[0] && g->profile.google_name[0] && g->fe.state != FE_WELCOME) {
        SDL_snprintf(g->pending_name, sizeof(g->pending_name), "%s", g->profile.google_name); /* Case A: only if still the default, below */
    }
    g->fill_name_from_provider = false;
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

/* What the lobby has for the game: a grant to follow, a guest session that ended, toasts. */
static void lobby_follow(Game *g, MgApp *app)
{
    Lobby *l = &g->lobby;
    if (l->to_menu_ready) { /* a guest: the host went back to the menu, the lobby stays */
        l->to_menu_ready = false;
        if (g->playing) {
            leave_play(g, app);
        }
    }
    if (l->declined_name[0]) {
        char msg[96];
        SDL_snprintf(msg, sizeof(msg), "%s %s", l->declined_name, str("invite_declined"));
        frontend_toast(&g->fe, msg, app->time);
        l->declined_name[0] = 0;
    }
    if (l->invite_error[0]) {
        char key[48];
        SDL_snprintf(key, sizeof(key), "invite_err_%s", l->invite_error);
        const char *msg = str(key);
        frontend_toast(&g->fe, msg == key ? str("invite_err_generic") : msg, app->time);
        l->invite_error[0] = 0;
    }
    for (int i = 0; i < l->invite_count; i++) {
        LobbyRequest *r = &l->invites[i];
        if (!r->shown) {
            r->shown = true;
            notice_push(&g->notice, NOTICE_INVITE, r->id, r->name, &r->skin, str("notice_invite"), LOBBY_INVITE_PANEL_S);
        }
    }
    if (l->grant_ready && l->grant.in_menu) { /* their lobby is in the menu: nothing to load */
        l->grant_ready = false;
        if (g->fpage.open) {
            g->fpage.open = false;
            g->fpage.editing_search = false;
            friends_page(&g->friends, false);
        }
        lobby_join_menu(l);
        session_set_identity(&g->session, g->profile.display_name, g->profile.skin);
    }
    if (l->grant_ready) {
        l->grant_ready = false;
        if (g->playing) { /* autosave + close our own session and lobby */
            leave_play(g, app);
        }
        if (g->fpage.open) {
            g->fpage.open = false;
            g->fpage.editing_search = false;
            friends_page(&g->friends, false);
        }
        g->pending_play = 4;
        g->load_step = 0;
        g->fe.depth = 0;
        g->fe.state = FE_LOADING;
    }
    if (l->join_error[0]) {
        char key[80];
        SDL_snprintf(key, sizeof(key), "join_err_%s", l->join_error);
        const char *msg = str(key);
        frontend_toast(&g->fe, msg == key ? str("join_err_generic") : msg, app->time);
        l->join_error[0] = 0;
    }
    if (l->ended) {
        frontend_toast(&g->fe, str(l->ended), app->time);
        l->ended = NULL;
        if (g->playing) {
            leave_play(g, app);
        } else {
            lobby_stop(l);
        }
    }
    if (l->joined_name[0]) {
        char msg[96];
        SDL_snprintf(msg, sizeof(msg), "%s %s", l->joined_name, str("lobby_entered"));
        if (l->joined_invited || !LOBBY_JOIN_APPROVAL) {
            notice_push(&g->notice, NOTICE_INFO, l->joined_name, l->joined_name, &l->joined_skin, str("lobby_entered"), NOTICE_INFO_S);
        } else {
            frontend_toast(&g->fe, msg, app->time);
        }
        l->joined_name[0] = 0;
    }
    for (int i = 0; i < l->request_count; i++) {
        LobbyRequest *r = &l->requests[i];
        if (!r->shown) {
            r->shown = true;
            notice_push(&g->notice, NOTICE_JOIN, r->id, r->name, &r->skin, str("notice_wants_join"), LOBBY_REQUEST_S);
        }
    }
}

/* The top-centre panel, over whatever is on screen; its buttons are taken before the stick */
static void draw_notices(Game *g, MgApp *app)
{
    const ui_frame_t f = frame_of(app);
    NoticeKind kind = NOTICE_JOIN;
    char id[NET_ID_LEN];
    const NoticeResult res = notice_panel_draw(&g->notice, &g->ui, &g->heads, &f, app->time, mg_minf(app->frame_dt, 0.1f), str("friend_accept"),
                                               str("friend_decline"), &kind, id);
    if (g->notice.just_shown) {
        if (g->notice.q[0].kind == NOTICE_JOIN) {
            lobby_request_shown(&g->lobby, g->notice.q[0].id);
        } else if (g->notice.q[0].kind == NOTICE_INVITE) {
            lobby_invite_shown(&g->lobby, g->notice.q[0].id);
        }
        if (g->settings.marker_sounds) {
            audio_cue(g->audio, AUDIO_CUE_TICK); /* two notes */
        }
        if (g->settings.haptics) {
            haptic_impact(false);
        }
    }
    if (res != NOTICE_NONE && kind == NOTICE_JOIN) {
        lobby_respond(&g->lobby, id, res == NOTICE_ACCEPT);
    } else if (res != NOTICE_NONE && kind == NOTICE_INVITE) {
        lobby_invite_answer(&g->lobby, id, res == NOTICE_ACCEPT);
    } else if (res != NOTICE_NONE && kind == NOTICE_FRIEND) {
        friends_respond(&g->friends, id, res == NOTICE_ACCEPT);
    }
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
        net_set_quiet(g->net, g->offline_play && !g->friends.signed_in); /* online, a solo game is a lobby */
        g->npc_epoch_ms = g->pending_play == 4 ? g->lobby.grant.start_tick : 0.0;
        g->save_play_base = g->save_rides_base = 0;
        if (g->pending_play == 3 && save_read(&g->save, g->profile_name)) { /* Continue */
            g->npc_resume_tick = g->save.npc_tick;
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
            g->npc_resume_tick = 0;
            g->save_play_base = (uint32_t)0 - (uint32_t)g->stats.play_seconds;
            g->save_rides_base = (uint32_t)0 - (uint32_t)g->stats.rides;
        }
        g->next_save = app->time + 30.0;
        autonav_clear(&g->autonav);
        g->active_marker = 0;
        g->cam.initialized = g->td.initialized = false;
        g->fade_snap = true;
        touch_controls_reset(&g->touch);
        if (g->pending_play == 4) {
            lobby_start_guest(&g->lobby);
            session_set_npc_epoch(&g->session, g->npc_epoch_ms);
        } else {
            g->lobby.save_hash = g->pending_play == 3 ? g->save.npc_seed : 0u;
            if (lobby_in_menu(&g->lobby) && lobby_is_host(&g->lobby)) { /* tick 0 is now, whatever the menu's backdrop has been doing */
                g->npc_epoch_ms = wall_clock_ms() - (double)g->npc_resume_tick / 0.06;
                session_set_npc_epoch(&g->session, g->npc_epoch_ms);
            }
            lobby_start_host(&g->lobby, 0, npc_seed_for(g, app)); /* online when signed in; offline nothing leaves the device */
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

#define MENU_NPC_SEED 0x4D454E55u /* a fixed crowd: nothing here is shared or saved */

/* Halfway between the menu's buttons and the screen edge, wherever those are on this device */
/* The players' spots: centred in the free space between the menu column and the lobby panel, the camera
 * backing off as the line grows so it always fits. */
static void menu_layout(Game *g, const MgApp *app)
{
    const FeState state = g->fe.state;
    const bool panel = state == FE_MAIN_MENU && g->lobby_visible;
    const int n = panel && g->menu_row_count > 0 ? g->menu_row_count : 1;
    const float free_l = frontend_menu_right(app, state), free_r = frontend_menu_free_right(app, panel);
    const float tan_half = HMM_TanF(HMM_AngleDeg(MENU_FOV_DEG) * 0.5f);
    const float line_m = (float)(n - 1) * MENU_SLOT_GAP + (n > 1 ? 1.5f : 1.1f);
    const float target = mg_maxf(MENU_HERO_DIST, line_m * app->height_pt / (2.0f * tan_half * mg_maxf(free_r - free_l, 80.0f)));
    g->menu_dist = g->menu_dist <= 0.0f ? target : g->menu_dist + (target - g->menu_dist) * mg_minf(app->frame_dt * 6.0f, 1.0f);
    g->menu_eye_z = MENU_HERO_Z + g->menu_dist;
    const HMM_Vec3 eye = HMM_V3(MENU_EYE_X, MENU_EYE_Y, g->menu_eye_z);
    const HMM_Vec3 fwd = HMM_NormV3(HMM_SubV3(HMM_V3(MENU_LOOK_X, MENU_LOOK_Y, MENU_LOOK_Z), eye));
    const float depth = (MENU_HERO_MID_Y - eye.Y) * fwd.Y + (MENU_HERO_Z - eye.Z) * fwd.Z;
    const float ndc = (free_l + free_r) * 0.5f / app->width_pt * 2.0f - 1.0f;
    const float aspect = app->width_pt / app->height_pt;
    const float cx = eye.X + ndc * depth * tan_half * aspect;
    for (int i = 0; i < LOBBY_MAX_PLAYERS; i++) {
        g->menu_slots[i] = HMM_V3(cx + ((float)i - (float)(n - 1) * 0.5f) * MENU_SLOT_GAP, MENU_HERO_FEET_Y, MENU_HERO_Z);
    }
}

/* The main menu's backdrop: the hospital from outside, its NPCs going about their day. The world
 * stays loaded for play. Off everywhere else, so a game starts its own sim from its own seed. */
static bool menu_world(Game *g, MgApp *app)
{
    menu_layout(g, app);
    const HMM_Vec3 hero = g->menu_slots[0];
    g->menu_hero = hero;
    const int st = g->fe.state;
    const bool onboarding = (st == FE_WELCOME || st == FE_USERNAME || st == FE_CREATOR) && !g->play_paused;
    if (st != FE_MAIN_MENU && st != FE_SETTINGS && st != FE_FRIENDS && !onboarding) {
        if (g->menu_world_on) {
            g->menu_world_on = false;
            npc_world_stop(&g->npcs);
            g->npcs_on = false;
        }
        return false;
    }
    if (!ensure_world(g, app)) {
        return false;
    }
    if (!g->menu_world_on) {
        g->menu_world_on = true;
        if (g->npcs_ok) {
            npc_world_start(&g->npcs, MENU_NPC_SEED);
            g->npcs_on = true;
            g->npc_catching_up = false;
        }
    }
    const float aspect = app->width_pt / app->height_pt;
    GfxSceneParams sp = {0};
    sp.eye = HMM_V3(MENU_EYE_X, MENU_EYE_Y, g->menu_eye_z);
    sp.view = HMM_LookAt_RH(sp.eye, HMM_V3(MENU_LOOK_X, MENU_LOOK_Y, MENU_LOOK_Z), HMM_V3(0, 1, 0));
    sp.proj = camera_projection(HMM_AngleDeg(MENU_FOV_DEG), aspect, 0.1f, 300.0f, 0.0f);
    g->view_proj = HMM_MulM4(sp.proj, sp.view);
    scene_defaults(&sp, (float)app->time, &g->day_state);
    gfx_set_scene(app->gfx, &sp);
    scene_fade_targets(g->scene, g->fade.target, 0, false, hero, sp.eye);
    floorfade_snap(&g->fade);
    SceneView view = {.eye = sp.eye, .focus = hero, .floor = 0, .time = (float)app->time, .lamp_on = g->lamp_on, .fade = g->fade.vis};
    scene_draw(g->scene, app->gfx, &view);
    scenery_draw(&g->scenery, app->gfx, &sp, (float)app->time, g->day_state.cloud);
    if (g->npcs_ok && g->npcs_on && g->npcs.pool.count > 0) {
        g->player.kin.pos = hero; /* NPC detail is by distance from here */
        draw_npcs(g, app, 0, g->frame_alpha);
    }
    return true;
}

/* --autotest lobbylayout: four players; panel, tags and avatars stay clear of each other and of the chip. */
static void lobby_layout_check(Game *g, MgApp *app, const FrontendCtx *ctx)
{
    const float w = app->width_pt, chip_b = 8.0f + 44.0f, col_r = frontend_menu_right(app, FE_MAIN_MENU);
    const float px = g->fe.lobby_x, py = g->fe.lobby_y, pw = g->fe.lobby_w, ph = g->fe.lobby_h;
    const char *bad = NULL;
    if (py < chip_b || py + ph > app->height_pt || px + pw > w || px < col_r) {
        bad = "the panel does not fit under the chip, right of the menu column";
    }
    for (int i = 0; i < ctx->row_count && !bad; i++) {
        const float head = 2.2f;
        const HMM_Vec3 top = HMM_AddV3(ctx->slots[i], HMM_V3(0, head, 0));
        const HMM_Vec4 c = HMM_MulM4V4(g->view_proj, HMM_V4V(top, 1.0f));
        const float x = (c.X / c.W * 0.5f + 0.5f) * w, y = (0.5f - c.Y / c.W * 0.5f) * app->height_pt;
        const float tw = text_width(&app->font, ctx->rows[i].name, 12.0f) * 0.5f + (ctx->rows[i].host ? 18.0f : 2.0f);
        if (x - tw < col_r || x + tw > px) {
            bad = "a name tag overlaps the menu column or the lobby panel";
        } else if (y < chip_b && x + tw > w - 200.0f) {
            bad = "a name tag overlaps the chip";
        }
        const HMM_Vec4 f1 = HMM_MulM4V4(g->view_proj, HMM_V4V(HMM_AddV3(ctx->slots[i], HMM_V3(-0.55f, 1.0f, 0)), 1.0f));
        const HMM_Vec4 f2 = HMM_MulM4V4(g->view_proj, HMM_V4V(HMM_AddV3(ctx->slots[i], HMM_V3(0.55f, 1.0f, 0)), 1.0f));
        const float al = (f1.X / f1.W * 0.5f + 0.5f) * w, ar = (f2.X / f2.W * 0.5f + 0.5f) * w;
        if (!bad && (al < col_r || ar > px)) {
            bad = "an avatar overlaps the menu column or the lobby panel";
        }
    }
    mg_log_info("autotest lobbylayout: %.0f x %.0f, panel %.0f,%.0f %.0f x %.0f, %d rows", (double)w, (double)app->height_pt, (double)px, (double)py, (double)pw, (double)ph, ctx->row_count);
    if (bad) {
        mg_log_error("autotest lobbylayout: %s", bad);
    }
    mg_log_info("AUTOTEST %s", bad ? "FAILED" : "PASSED");
    app->quit_requested = true;
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
    g->ui.input_off = account_link_active(&g->link); /* the screens underneath take no touches */
    g->lobby_visible = lobby_in_menu(&g->lobby) && g->friends.signed_in;
    g->menu_row_count = g->lobby_visible ? lobby_roster(&g->lobby, g->menu_rows) : 0;
    if (g->demo_lobby) { /* perf and layout: a full lobby without the network */
        static const char *const names[LOBBY_MAX_PLAYERS] = {"Ana", "Bogdan", "Clara", "Dan"};
        g->lobby_visible = true;
        g->menu_row_count = LOBBY_MAX_PLAYERS;
        for (int i = 0; i < LOBBY_MAX_PLAYERS; i++) {
            LobbyRow *r = &g->menu_rows[i];
            SDL_zerop(r);
            SDL_snprintf(r->id, sizeof(r->id), "demo%d", i);
            SDL_snprintf(r->name, sizeof(r->name), "%s", names[i]);
            r->skin = AVATAR_DEFAULT_SKIN;
            r->skin.suit = (uint8_t)(i * 2);
            r->skin.hair = (uint8_t)(i + 1);
            r->host = i == 0;
            r->me = i == 0;
        }
    }
    const bool world_behind = menu_world(g, app);
    FrontendCtx ctx = {g->profile.display_name,
                       &g->profile.skin,
                       g->profile.guest || !g->profile_loaded,
                       g->prefs.auth_provider,
                       g->net && google_auth_available(g->google),
                       g->net && apple_auth_available(g->google),
                       g->google_busy,
                       g->has_save,
                       world_behind,
                       g->menu_hero,
                       g->friends.incoming,
                       &g->heads,
                       g->load_step / 2.0f,
                       MG_VERSION};
    ctx.lobby_visible = g->lobby_visible;
    ctx.lobby_host = g->demo_lobby || lobby_is_host(&g->lobby);
    ctx.lobby_open = g->lobby.open;
    ctx.rows = g->menu_rows;
    ctx.row_count = g->menu_row_count;
    memcpy(ctx.slots, g->menu_slots, sizeof(ctx.slots));
    FeAction a = frontend_frame(f, &g->ui, app, &ctx, &g->avatar_meshes);
    if (g->demo_lobby && !g->perf.active && f->state == FE_MAIN_MENU && app->time - g->start_time > 3.0 && g->start_time > 0.0) {
        lobby_layout_check(g, app, &ctx);
    }
    if (f->state == FE_MAIN_MENU && world_behind && g->lobby_visible) { /* the players' name tags, the same style as in play */
        const float head = 2.2f;
        for (int e = 0; e < (int)MG_ARRAY_COUNT(f->avatars); e++) {
            const FeAvatar *av = &f->avatars[e];
            if (!av->active || av->fade < 0.5f) {
                continue;
            }
            const HMM_Vec3 top = HMM_AddV3(ctx.slots[av->at], HMM_V3(0, head, 0));
            HMM_Vec4 c = HMM_MulM4V4(g->view_proj, HMM_V4V(top, 1.0f));
            if (av->host && c.W > 0.01f) {
                const float x = (c.X / c.W * 0.5f + 0.5f) * app->width_pt, y = (0.5f - c.Y / c.W * 0.5f) * app->height_pt;
                frontend_draw_crown(app->gfx, &app->font, x - text_width(&app->font, av->name, 12.0f) * 0.5f - 16.0f, y - 6.0f, 12.0f);
            }
            name_tag(g, app, top, av->name);
        }
    }
    if (f->state == FE_FRIENDS && friends_page_draw(&g->fpage, &g->ui, &g->friends, &g->lobby, &g->heads, app, g->profile.display_name, &g->profile.skin)) {
        frontend_back(f);
    }
    draw_notices(g, app);
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
        g->ui.input_off = false;
        account_link_draw(&g->link, &g->ui, &g->heads, app, app->time);
        return;
    }
    g->ui.input_off = false;
    account_link_draw(&g->link, &g->ui, &g->heads, app, app->time);
    if (f->clicked) {
        audio_click(g->audio);
    }
    switch (a) {
    case FE_SIGN_IN_GOOGLE:
#ifdef MG_TEST_AUTH
        test_sign_in(g);
        break;
#endif
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
    case FE_OPEN_FRIENDS:
        friends_page_open(&g->fpage, &g->friends);
        frontend_go(f, FE_FRIENDS);
        break;
    case FE_OPEN_INVITE:
        friends_page_open(&g->fpage, &g->friends);
        g->fpage.invite_mode = true;
        g->fpage.tab = 0;
        frontend_go(f, FE_FRIENDS);
        break;
    case FE_LOBBY_TOGGLE:
        lobby_set_open(&g->lobby, !g->lobby.open);
        g->prefs.lobby_open = g->lobby.open;
        save_prefs(g);
        break;
    case FE_LOBBY_KICK:
        lobby_kick(&g->lobby, f->kick_id);
        break;
    case FE_LOBBY_LEAVE:
        lobby_stop(&g->lobby);
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
    app_text_input(app, f->editing_name || friends_page_editing(&g->fpage));
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

static uint32_t hash_text(const char *t)
{
    uint32_t h = 2166136261u;
    while (*t) {
        h = (h ^ (uint8_t)*t++) * 16777619u;
    }
    return h | 1u;
}

/* Everyone in a room derives the same roster from its name; offline it is the save's */
static uint32_t npc_seed_for(Game *g, MgApp *app)
{
    if (lobby_is_guest(&g->lobby) || g->pending_play == 4) {
        return (uint32_t)g->lobby.grant.npc_seed; /* the host's roster */
    }
    if (!g->offline_play && g->session.mode != MODE_OFFLINE && g->session.topic[0]) {
        return hash_text(g->session.topic);
    }
    const char *forced = arg_value(app->argc, app->argv, "--npc-seed");
    if (forced) {
        return (uint32_t)SDL_atoi(forced) | 1u;
    }
    if (!g->save.npc_seed) {
        g->save.npc_seed = (uint32_t)SDL_GetPerformanceCounter() | 1u;
    }
    return g->save.npc_seed;
}

static double wall_clock_ms(void)
{
    SDL_Time t = 0;
    SDL_GetCurrentTime(&t);
    return (double)t / 1e6;
}

static void npc_save_check_path(char *path, size_t size)
{
#if defined(SDL_PLATFORM_IOS)
    const char *dir = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
    SDL_snprintf(path, size, "%s/npc_save_check.txt", dir ? dir : ".");
#else
    char *dir = SDL_GetPrefPath("docvex", "mobilegame");
    SDL_snprintf(path, size, "%snpc_save_check.txt", dir ? dir : "");
    SDL_free(dir);
#endif
}

/* --autotest npcs-save: play, save, die without any goodbye */
static void npc_save_test_save(Game *g, MgApp *app)
{
    const char *at = arg_value(app->argc, app->argv, "--autotest");
    if (!at || SDL_strcmp(at, "npcs-save") != 0 || !g->npcs_on || g->npc_catching_up || g->npcs.tick < 1200u) {
        return;
    }
    write_save(g);
    char path[640], text[64];
    npc_save_check_path(path, sizeof(path));
    const int n = SDL_snprintf(text, sizeof(text), "%u %08x %u\n", g->npcs.tick, npc_world_hash(&g->npcs), g->npcs.seed);
    SDL_IOStream *io = SDL_IOFromFile(path, "w");
    if (io) {
        SDL_WriteIO(io, text, (size_t)n);
        SDL_CloseIO(io);
    }
    mg_log_info("autotest npcs-save: saved at tick %u hash %08x seed %u", g->npcs.tick, npc_world_hash(&g->npcs), g->npcs.seed);
    mg_log_info("AUTOTEST PASSED");
    _Exit(0);
}

/* --autotest npcs-continue: at the saved tick every position must match what was saved */
static void npc_save_test_compare(Game *g, MgApp *app)
{
    const char *at = arg_value(app->argc, app->argv, "--autotest");
    if (!at || SDL_strcmp(at, "npcs-continue") != 0) {
        return;
    }
    char path[640];
    npc_save_check_path(path, sizeof(path));
    size_t len = 0;
    char *text = (char *)SDL_LoadFile(path, &len);
    uint32_t tick = 0, hash = 0, seed = 0;
    const bool read = text && SDL_sscanf(text, "%u %x %u", &tick, &hash, &seed) == 3;
    SDL_free(text);
    const uint32_t now = npc_world_hash(&g->npcs);
    const bool ok = read && tick == g->npcs.tick && hash == now && seed == g->npcs.seed;
    if (!ok) {
        mg_log_error("autotest npcs-continue: assert same_state: saved tick %u hash %08x seed %u, restored tick %u hash %08x seed %u", tick, hash, seed, g->npcs.tick, now, g->npcs.seed);
    }
    mg_log_info("autotest npcs-continue: tick %u hash %08x, saved %08x: %s", g->npcs.tick, now, hash, ok ? "identical" : "DIFFERENT");
    mg_log_info("AUTOTEST %s", ok ? "PASSED" : "FAILED");
    app->quit_requested = true;
}

/* One sim tick, plus what the tests watch */
static void npc_step(Game *g, MgApp *app)
{
    const size_t mark = arena_mark(&app->frame);
    npc_tick(&g->npcs, app->jobs, &app->frame, MG_TICK_DT);
    arena_reset_to(&app->frame, mark);
    if (g->npc_sync_log && g->playing && g->npcs.tick % 600u == 0u) { /* not the menu's backdrop */
        mg_log_info("npc-sync: tick %u hash %08x alive %d seed %u", g->npcs.tick, npc_world_hash(&g->npcs), g->npcs.pool.count, g->npcs.seed);
        g->npc_sync_tick = g->npcs.tick;
        g->npc_sync_hash = npc_world_hash(&g->npcs);
    }
}

/* Joining a room, or Continue: replay from the seed up to the tick the others are on, at most 1 ms per
 * frame. The NPCs appear when it has caught up. Online the target keeps moving with the clock. */
static void npc_catch_up(Game *g, MgApp *app)
{
    if (!g->npcs_on || !g->npc_catching_up) {
        return;
    }
    const bool online = !g->offline_play && g->session.mode != MODE_OFFLINE;
    const double deadline = mg_time_now() + 0.001;
    const uint32_t target = online ? (g->npc_epoch_ms > 0.0 ? (uint32_t)((wall_clock_ms() - g->npc_epoch_ms) * 0.06) : 0u) : g->npc_resume_tick;
    PROF_SCOPE("npc.catch_up");
    while (g->npcs.tick < target && mg_time_now() < deadline) {
        npc_step(g, app);
    }
    if (g->npcs.tick >= target) {
        g->npc_catching_up = false;
        mg_log_info("npc: caught up at tick %u", g->npcs.tick);
        npc_save_test_compare(g, app);
    }
}

/* Online: tick 0 of the room is the earliest epoch anyone in it published */
static void npc_room_epoch(Game *g)
{
    if (g->offline_play || g->session.mode == MODE_OFFLINE || g->session.status != NET_JOINED) {
        return;
    }
    if (g->npc_epoch_ms <= 0.0) {
        g->npc_epoch_ms = wall_clock_ms();
        session_set_npc_epoch(&g->session, g->npc_epoch_ms);
    }
    double earliest = g->npc_epoch_ms;
    for (int i = 0; i < SESSION_MAX_REMOTE; i++) {
        const RemotePlayer *r = &g->session.remotes[i];
        if (r->active && r->meta.npc_epoch_ms > 0.0 && r->meta.npc_epoch_ms < earliest) {
            earliest = r->meta.npc_epoch_ms;
        }
    }
    if (earliest < g->npc_epoch_ms - 0.5) { /* someone was here first: replay from their tick 0 */
        g->npc_epoch_ms = earliest;
        session_set_npc_epoch(&g->session, earliest);
        npc_world_start(&g->npcs, g->npcs.seed);
        g->npc_catching_up = true;
        mg_log_info("npc: joining a room in progress, epoch %.0f", earliest);
    }
}

/* Small life signs, worked out where the NPC is drawn from its id and the clock: nothing here feeds
 * back into the sim, and the only per-NPC state is the smoothed head direction. */
static void npc_idle_touches(Game *g, MgApp *app, int i, const npc_hot_t *h, npc_cold_t *c, AvatarPose *pose, float dt, bool queue_waiting)
{
    const float t = (float)app->time + (float)c->id * 3.7f;
    float head_target = 0.0f;
    if (c->phase == NPC_PHASE_HOLD) {
        if (c->pose == POSE_STAND) { /* weight shift, every few seconds */
            pose->weight_tilt = HMM_AngleDeg(2.0f) * HMM_SinF(t * 1.3f);
        }
        if (c->pose == POSE_STAND || c->pose == POSE_SIT) { /* looks at whoever walks past, otherwise glances around */
            uint16_t near[12];
            const int n = spatial_query(&g->npcs.grid, h->storey, h->x, h->z, 3.0f, near, 12);
            const npc_hot_t *hot = g->npcs.pool.hot[g->npcs.pool.read];
            float best = 9.0f;
            bool passer = false;
            for (int k = 0; k < n; k++) {
                const npc_hot_t *o = &hot[near[k]];
                const float ox = o->x - h->x, oz = o->z - h->z, d2 = ox * ox + oz * oz;
                if (near[k] != i && (o->vx != 0.0f || o->vz != 0.0f) && d2 < best) {
                    float rel = fmodf(atan2f(-ox, -oz) - h->yaw + 3.0f * HMM_PI32, 2.0f * HMM_PI32) - HMM_PI32;
                    best = d2;
                    head_target = mg_clampf(rel, -HMM_AngleDeg(40.0f), HMM_AngleDeg(40.0f));
                    passer = true;
                }
            }
            if (!passer) {
                head_target = HMM_AngleDeg(20.0f) * HMM_SinF(t * 0.45f);
            }
        }
        if (c->pose == POSE_SIT) { /* one seated stretch in ten, every 20 s */
            const float cycle = fmodf(t, 20.0f);
            if (cycle < 0.9f && (((uint32_t)(t / 20.0f) + c->id) % 10u) == 0u) {
                pose->stretch = HMM_SinF(cycle / 0.9f * HMM_PI32);
            }
        }
        if (queue_waiting && c->pose == POSE_STAND && SDL_strcmp(g->npcs.data.archetypes[c->archetype].name, "receptionist") == 0) {
            const float cycle = fmodf(t, 9.0f); /* a nod */
            if (cycle < 1.0f) {
                pose->head_pitch = HMM_AngleDeg(12.0f) * HMM_SinF(cycle * HMM_PI32);
            }
        }
    }
    /* the active NPC who wants to talk lifts its head and looks at the player */
    const bool chatting = g->ia_ok && g->ia.active == c->ia_slot && c->wants_chat;
    if (chatting) {
        const float px = g->player.kin.pos.X - h->x, pz = g->player.kin.pos.Z - h->z;
        const float rel = fmodf(atan2f(-px, -pz) - pose->yaw + 3.0f * HMM_PI32, 2.0f * HMM_PI32) - HMM_PI32;
        head_target = mg_clampf(rel, -HMM_AngleDeg(70.0f), HMM_AngleDeg(70.0f));
    }
    c->vis_pop += ((chatting ? 1.0f : 0.0f) - c->vis_pop) * mg_minf(dt / 0.25f * 3.0f, 1.0f);
    pose->head_lift = 0.12f * c->vis_pop;
    if (c->talk_t > 0.0f) {
        c->talk_t = mg_maxf(c->talk_t - dt, 0.0f);
        const float since = 3.0f - c->talk_t;
        if (since < 0.6f) { /* nods once */
            pose->head_pitch = HMM_AngleDeg(14.0f) * HMM_SinF(since / 0.6f * HMM_PI32);
        }
    }
    c->vis_head += (head_target - c->vis_head) * mg_minf(dt * 5.0f, 1.0f);
    pose->head_yaw = c->vis_head;
}

/* The panel was tapped (or E on desktop): use what it shows, once */
static void interact_tap(Game *g, MgApp *app)
{
    (void)app;
    if (ia_use_active(&g->ia)) {
        g->panel.punch = 1.0f;
        g->panel.head_tap = 1.0f;
        if (g->settings.marker_sounds) {
            audio_cue(g->audio, AUDIO_CUE_TICK);
        }
        if (g->settings.haptics) {
            haptic_impact(false);
        }
    }
}

static void lift_panel_press(Game *g, Elevator *el, int floor)
{
    if (floor != el->car_floor && g->lift_panel.pressed < 0) {
        audio_ding(g->audio);
    }
    if (g->settings.haptics) {
        haptic_impact(false);
    }
    elevator_panel_press(&g->lift_panel, floor, el->car_floor);
}

static void on_marker_interact(uint16_t ref, void *user)
{
    Game *g = (Game *)user;
    for (int i = 0; i < MARKER_MAX; i++) {
        marker_t *m = &g->markers.m[i];
        if (m->id && (uint16_t)m->id == ref && m->state != MARKER_REACHED) {
            if (g->active_marker == m->id) {
                autonav_clear(&g->autonav);
                g->active_marker = 0;
            }
            marker_reached(g, g->ia_app, m);
            return;
        }
    }
}

/* "Talk": a one-line greeting for 2.5 s, the NPC faces the player and nods */
static void on_npc_interact(uint16_t ref, void *user)
{
    Game *g = (Game *)user;
    NpcPool *p = &g->npcs.pool;
    for (int i = 0; i < p->count; i++) {
        npc_cold_t *c = &p->cold[i];
        if ((uint16_t)c->id == ref) {
            const int n = g->npcs.data.n_greetings[c->archetype];
            const char *line = n ? g->npcs.data.greetings[c->archetype][(g->greet_count++ + c->id) % (uint32_t)n] : "";
            SDL_snprintf(g->greet_name, sizeof(g->greet_name), "%s", c->name);
            SDL_snprintf(g->greet_line, sizeof(g->greet_line), "%s", line);
            g->greet_t = 2.5f;
            c->talk_t = 3.0f;
            if (g->npcs.on_interact) {
                g->npcs.on_interact(c->id, g->npcs.callback_user);
            }
            return;
        }
    }
}

static void npc_registered(int index, void *user)
{
    Game *g = (Game *)user;
    npc_cold_t *c = &g->npcs.pool.cold[index];
    const npc_hot_t *h = &g->npcs.pool.hot[g->npcs.pool.read][index];
    const preview_desc_t pv = {GFX_MESH_NONE, &c->look, 1.0f, 0.0f, 0.0f, HMM_V4(1, 1, 1, 1)};
    c->ia_slot = ia_add(&g->ia, IA_NPC, HMM_V3(h->x, h->y, h->z), IA_ENTER_RANGE, h->storey, 0, (uint16_t)c->id, c->name, on_npc_interact, g, pv);
}

static void npc_unregistered(int index, void *user)
{
    Game *g = (Game *)user;
    ia_remove(&g->ia, g->npcs.pool.cold[index].ia_slot);
}

/* Buttons and the lift, from the room graph */
static void interact_setup(Game *g, MgApp *app)
{
    g->ia_app = app;
    g->no_panels = has_flag(app->argc, app->argv, "--no-panels");
    const char *ia_at = arg_value(app->argc, app->argv, "--autotest");
    g->text_test = ia_at && SDL_strcmp(ia_at, "text") == 0;
    g->hud_clamp_test = ia_at && SDL_strcmp(ia_at, "hudclamp") == 0;
    g->ia_ok = ia_init(&g->ia, &app->perm) && preview_panel_init(&g->panel, app->gfx, &app->perm);
    g->npcs.on_spawned = npc_registered;
    g->npcs.on_despawned = npc_unregistered;
    g->npcs.callback_user = g;
    for (int i = 0; i < MARKER_MAX; i++) {
        g->marker_ia[i] = IA_NONE;
    }
    g->lift_panel_ok = elevator_panel_init(&g->lift_panel, app->gfx, &app->perm, scene_floor_count(), &app->font, scene_floor_name);
    if (!g->ia_ok) {
        return;
    }
    const preview_desc_t any = {g->unit_box, NULL, 0.3f, 0.0f, 0.0f, HMM_V4(0.4f, 0.8f, 0.9f, 1.0f)};
    g->ia_any_slot = ia_add(&g->ia, IA_BUTTON, HMM_V3(0, -100.0f, 0), 0.1f, 99, IA_DISABLED | IA_FACING_FREE, 0, "Mesh", NULL, NULL, any);
    g->ia_check = SDL_strcmp(arg_value(app->argc, app->argv, "--autotest") ? arg_value(app->argc, app->argv, "--autotest") : "", "interact") == 0;
    g->ia_hover_slot = IA_NONE;
}

/* Developer -> Show ranges (enter ring, thin exit ring; yellow = active), Show active id */
static void draw_ia_debug(Game *g, MgApp *app, int floor)
{
    if (!g->ia_ok || !(g->settings.show_ia_ranges || g->settings.show_ia_active)) {
        return;
    }
    const Interact *ia = &g->ia;
    for (int s = 0; g->settings.show_ia_ranges && s < ia->high_water; s++) {
        const ia_hot_t *h = &ia->hot[s];
        if (!h->used || grid_of_storey(h->storey) != floor || s == g->ia_any_slot) {
            continue;
        }
        const float y = scene_floor_y(g->scene, floor) + 0.02f, en = h->range, ex = en + (IA_EXIT_RANGE - IA_ENTER_RANGE);
        GfxMaterial m = mat_unlit(s == ia->active ? HMM_V4(1.0f, 0.85f, 0.1f, 1.0f) : HMM_V4(0.2f, 0.9f, 0.5f, 1.0f));
        HMM_Mat4 t = HMM_MulM4(HMM_Translate(HMM_V3(h->pos.X, y, h->pos.Z)), HMM_Scale(HMM_V3(en, 1.0f, en)));
        gfx_draw(app->gfx, g->markers.ring, &t, &m, GFX_DRAW_NO_SHADOW);
        t = HMM_MulM4(HMM_Translate(HMM_V3(h->pos.X, y, h->pos.Z)), HMM_Scale(HMM_V3(ex, 1.0f, ex)));
        gfx_draw(app->gfx, g->markers.ring, &t, &m, GFX_DRAW_NO_SHADOW);
    }
    if (g->settings.show_ia_active) {
        char line[96];
        if (ia->active == IA_NONE) {
            SDL_snprintf(line, sizeof(line), "interact: none (scan %u)", ia->scans);
        } else {
            const ia_hot_t *h = &ia->hot[ia->active];
            SDL_snprintf(line, sizeof(line), "interact: #%u %s ref %u%s", (unsigned)ia->active, ia->cold[ia->active].label ? ia->cold[ia->active].label : "-",
                         (unsigned)h->ref, (h->flags & IA_DISABLED) ? " (grey)" : "");
        }
        text_draw(app->gfx, &app->font, app->safe_left + 12.0f, app->height_pt - app->safe_bottom - 30.0f, 14.0f, ui_rgba(0xFFE680FF), line);
    }
}

/* The greeting card: bottom right, beside the preview */
static void greet_rect(const Game *g, const MgApp *app, float r[4])
{
    const ui_frame_t cf = hud_frame(app);
    const float cw = mg_minf(ui_safe_w(&cf) * 0.5f, 420.0f), ch = 60.0f;
    r[0] = ui_safe_right(&cf) - UI_MARGIN - cf.corner - g->panel.size - 12.0f - cw;
    r[1] = ui_safe_bottom(&cf) - UI_MARGIN - cf.corner - ch;
    r[2] = cw;
    r[3] = ch;
}

/* --autotest interact: where its targets are */
static void interact_probe(Game *g, MgApp *app, AutotestProbe *pr)
{
    const Interact *ia = &g->ia;
    pr->active_slot = ia->active == IA_NONE ? -1 : (int)ia->active;
    pr->active_flags = ia->active == IA_NONE ? 0u : ia->hot[ia->active].flags;
    SDL_memcpy(pr->ia_fired, ia->fired, sizeof(pr->ia_fired));
    pr->ia_enter_lag = g->ia_enter_lag;
    pr->ia_exit_lag = g->ia_exit_lag;
    pr->greeting = g->greet_t > 0.0f;
    pr->lift_at = HMM_V3((g->rooms.lift[0] + g->rooms.lift[2]) * 0.5f, 0.1f, (g->rooms.lift[1] + g->rooms.lift[3]) * 0.5f);
    for (int i = 0; i < 2; i++) {
        pr->it_slot[i] = -1;
    }
    for (int s = 0; s < ia->high_water; s++) { /* the marker */
        const ia_hot_t *h = &ia->hot[s];
        if (h->used && h->kind == IA_MARKER && pr->it_slot[0] < 0) {
            pr->it_slot[0] = s;
            pr->it_stand[0] = HMM_V3(h->pos.X, 0.1f, h->pos.Z);
        }
    }
    const NpcPool *p = &g->npcs.pool;
    const npc_hot_t *hot = p->hot[p->read];
    for (int i = 0; i < p->count && g->npcs_on; i++) {
        const npc_cold_t *c = &p->cold[i];
        if (c->wants_chat && SDL_strcmp(g->npcs.data.archetypes[c->archetype].name, "receptionist") == 0) {
            pr->it_slot[1] = c->ia_slot;
            pr->it_stand[1] = HMM_V3(hot[i].x, 0.1f, hot[i].z + 1.35f); /* the public side of the desk */
            break;
        }
    }
    for (int f = 0; f < 3 && g->lift_panel_ok; f++) {
        elevator_panel_button_pos(&g->lift_panel, f, &pr->lift_btn_x[f], &pr->lift_btn_y[f]);
    }
#if !defined(SDL_PLATFORM_IOS) && !defined(SDL_PLATFORM_ANDROID)
    /* something on screen and far away, for the mouse */
    pr->hover_slot = -1;
    const HMM_Vec3 pos = g->player.kin.pos;
    for (int s = 0; s < ia->high_water && pr->hover_slot < 0; s++) {
        const ia_hot_t *h = &ia->hot[s];
        const float dx = h->pos.X - pos.X, dz = h->pos.Z - pos.Z;
        const bool still = h->kind != IA_NPC || (h->flags & IA_WANTS_CHAT); /* posted people stand still */
        if (!h->used || !still || s == g->ia_any_slot || grid_of_storey(h->storey) != grid_of_storey(g->player_storey_cached) || dx * dx + dz * dz < 8.0f) {
            continue;
        }
        HMM_Vec4 c = HMM_MulM4V4(g->view_proj, HMM_V4(h->pos.X, scene_floor_y(g->scene, grid_of_storey(h->storey)), h->pos.Z, 1.0f));
        const float nx = c.X / c.W, ny = c.Y / c.W;
        if (c.W > 0.0f && fabsf(nx) < 0.8f && fabsf(ny) < 0.8f) {
            pr->hover_slot = s;
            pr->hover_x = (nx * 0.5f + 0.5f) * app->width_pt;
            pr->hover_y = (0.5f - ny * 0.5f) * app->height_pt;
        }
    }
#else
    pr->hover_slot = -1;
    (void)app;
#endif
}

/* Keep the registry in step with what moved, then choose the active one */
static void interact_tick(Game *g, MgApp *app)
{
    if (!g->ia_ok || !g->playing) {
        return;
    }
    PROF_SCOPE("interact");
    NpcPool *p = &g->npcs.pool;
    const npc_hot_t *hot = p->hot[p->read];
    const bool chat_eval = app->clock.tick % 30u == 0u; /* twice a second */
    for (int i = 0; i < p->count && g->npcs_on; i++) {
        npc_cold_t *c = &p->cold[i];
        if (chat_eval) {
            const char *arch = g->npcs.data.archetypes[c->archetype].name;
            const uint32_t tags = c->slot != SLOT_NONE ? g->rooms.slots[c->slot].tags : 0u;
            const bool posted = c->phase == NPC_PHASE_HOLD;
            c->wants_chat = posted && (SDL_strcmp(arch, "receptionist") == 0 || (SDL_strcmp(arch, "doctor") == 0 && (tags & (SLOT_OFFICE_DESK | SLOT_EXAM_DOCTOR))) ||
                                       (SDL_strcmp(arch, "nurse") == 0 && (tags & SLOT_NURSE_STATION)) || c->pose == POSE_SIT);
            const NpcArchetype *ar = &g->npcs.data.archetypes[c->archetype];
            const bool first = ar->interact_priority && (!ar->priority_tags || (tags & ar->priority_tags));
            ia_set_flags(&g->ia, c->ia_slot, c->wants_chat ? (IA_WANTS_CHAT | (first ? IA_PRIORITY : 0u)) : IA_DISABLED);
        }
        ia_set_pos(&g->ia, c->ia_slot, HMM_V3(hot[i].x, hot[i].y, hot[i].z), hot[i].storey);
        g->ia.cold[c->ia_slot].label = c->name; /* despawning moves NPCs in the array */
        g->ia.cold[c->ia_slot].preview.look = &c->look;
        g->ia.hot[c->ia_slot].ref = (uint16_t)c->id;
    }
    const preview_desc_t diamond = {g->markers.diamond, NULL, 1.0f, 0.0f, 0.0f, HMM_V4(1.0f, 0.4f, 0.3f, 1.0f)};
    for (int i = 0; i < MARKER_MAX; i++) {
        const marker_t *m = &g->markers.m[i];
        if (m->id != g->marker_ia_id[i]) {
            if (g->marker_ia[i] != IA_NONE) {
                ia_remove(&g->ia, g->marker_ia[i]);
                g->marker_ia[i] = IA_NONE;
            }
            g->marker_ia_id[i] = m->id;
            if (m->id) {
                g->marker_ia[i] = ia_add(&g->ia, IA_MARKER, m->pos, 1.6f, m->storey, IA_FACING_FREE, (uint16_t)m->id, "Marker", on_marker_interact, g, diamond);
            }
        }
    }
    const HMM_Vec3 pos = g->player.kin.pos;
    const int storey = scene_indoors(g->scene, pos) ? scene_floor_of(g->scene, pos) : -1;
    g->player_storey_cached = storey;
    const uint16_t before = g->ia.active;
    ia_tick(&g->ia, &app->frame, app->clock.tick, pos, storey, g->player.yaw);
    /* desktop: the mouse over something shows it too */
    uint16_t hover = IA_NONE;
    if (app->input.mouse_seen && !g->menu.open && !g->perf.active && (g->autotest.kind == 0 || g->ia_check)) {
        HMM_Vec3 hit;
        if (screen_to_floor(g, app, app->input.mouse_x, app->input.mouse_y, scene_floor_y(g->scene, storey < 0 ? 0 : storey), &hit)) {
            hover = ia_near_point(&g->ia, hit.X, hit.Z, storey, 0.8f);
        }
    }
    if (hover != IA_NONE) {
        g->ia.active = hover;
    }
    if (g->hud_clamp_test) { /* every panel up at once, while 16 markers go round the edge */
        g->settings.preview_any = g->settings.force_lift_panel = true;
        g->greet_t = 2.0f;
        SDL_snprintf(g->greet_line, sizeof(g->greet_line), "%s", str("autotest_text"));
    }
    if (g->settings.preview_any) {
        const GfxMeshId meshes[] = {g->unit_box, g->markers.diamond, g->markers.ring, g->avatar_meshes.shadow};
        const uint32_t pick = (app->clock.tick / 120u) % 4u;
        g->ia.cold[g->ia_any_slot].preview.mesh = meshes[pick];
        g->ia.active = g->ia_any_slot;
    }
    if (g->ia_check && !g->settings.preview_any) {
        const uint16_t want = ia_best_brute(&g->ia, pos, storey, g->player.yaw);
        g->ia_enter_run = g->ia.active == IA_NONE && want != IA_NONE ? g->ia_enter_run + 1 : 0;
        g->ia_enter_lag = SDL_max(g->ia_enter_lag, g->ia_enter_run);
        bool stale = false;
        if (g->ia.active != IA_NONE && hover == IA_NONE) {
            const ia_hot_t *h = &g->ia.hot[g->ia.active];
            const float dx = h->pos.X - pos.X, dz = h->pos.Z - pos.Z, r = h->range + (IA_EXIT_RANGE - IA_ENTER_RANGE) + 0.05f;
            stale = dx * dx + dz * dz > r * r;
        }
        g->ia_exit_run = stale ? g->ia_exit_run + 1 : 0;
        g->ia_exit_lag = SDL_max(g->ia_exit_lag, g->ia_exit_run);
    }
    if (g->ia_log && g->ia.active != before) {
        mg_log_info("interact: active %s", g->ia.active == IA_NONE ? "none" : g->ia.cold[g->ia.active].label);
    }
}

/* "..." clouds over those who want to talk: the active NPC, plus up to six faint ones nearby */
static void draw_cloud_at(MgApp *app, float x, float y, bool active, float vis, float k)
{
    const float t = (float)app->time;
    y += active ? 3.0f * HMM_SinF(t * 2.0f * HMM_PI32) : 0.0f;
    const uint8_t alpha = (uint8_t)((active ? 235.0f : 95.0f) * vis);
    const UiColor white = {255, 255, 255, alpha};
    ui_round_rect(app->gfx, &app->font, x - 19.0f * k, y - 12.0f * k, 38.0f * k, 24.0f * k, 12.0f * k, white); /* one shape: overlapping ones show through each other */
    for (int d = 0; d < 3; d++) {
        const float phase = active ? fmodf(t / 0.9f + (float)d / 3.0f, 1.0f) : 0.0f; /* in sequence */
        const float r = (active ? 1.8f + 1.4f * HMM_SinF(phase * HMM_PI32) : 2.0f) * k;
        const UiColor dot = {40, 44, 54, alpha};
        ui_circle(app->gfx, &app->font, x + ((float)d * 7.0f - 7.0f) * k, y, r, dot);
    }
}

static void draw_one_cloud(Game *g, MgApp *app, HMM_Vec3 at, bool active, float vis)
{
    HMM_Vec4 c = HMM_MulM4V4(g->view_proj, HMM_V4V(at, 1.0f));
    if (c.W > 0.01f) {
        draw_cloud_at(app, (c.X / c.W * 0.5f + 0.5f) * app->width_pt, (0.5f - c.Y / c.W * 0.5f) * app->height_pt, active, vis, 1.0f);
    }
}

static void draw_chat_bubbles(Game *g, MgApp *app, int floor)
{
    const int mode = g->settings.chat_bubbles;
    if (mode == 0 || !g->npcs_on || !g->ia_ok || g->npc_catching_up) {
        return;
    }
    const NpcPool *p = &g->npcs.pool;
    const npc_hot_t *hot = p->hot[p->read];
    const HMM_Vec3 me = g->player.kin.pos;
    int faint = 0;
    for (int i = 0; i < p->count; i++) {
        const npc_hot_t *h = &hot[i];
        const npc_cold_t *c = &p->cold[i];
        if (!c->wants_chat || (h->storey < 0 ? 0 : (int)h->storey) != floor) {
            continue;
        }
        const bool active = g->ia.active == c->ia_slot;
        const float dx = h->x - me.X, dz = h->z - me.Z;
        if (!active && (mode < 2 || faint >= 6 || dx * dx + dz * dz > 4.0f * 4.0f)) {
            continue;
        }
        faint += !active;
        const float top = c->pose == POSE_BED ? 1.1f : (c->pose == POSE_SIT ? 1.85f : 2.1f);
        const float sx = c->pose == POSE_SIT ? c->seat_x : h->x, sz = c->pose == POSE_SIT ? c->seat_z : h->z;
        draw_one_cloud(g, app, HMM_V3(sx, h->y + top + 0.35f, sz), active, g->fade.vis[h->storey < 0 ? 0 : h->storey]);
    }
}

/* M7 roster: culled and given a LOD on the job system, drawn on the main thread */
static void draw_npcs(Game *g, MgApp *app, int floor, float alpha)
{
    NpcPool *p = &g->npcs.pool;
    const npc_hot_t *hot = p->hot[p->read];
    const float dt = mg_minf(app->frame_dt, 0.1f), now = (float)g->npcs.tick + alpha;
    const bool half = g->settings.npc_density < 1.0f && !perf_wants_npcs(g);
    const HMM_Vec3 me = g->player.kin.pos;
    uint8_t *lods = ARENA_PUSH(&app->frame, uint8_t, (size_t)p->count);
    if (!lods) {
        return;
    }
    const float near_m = g->thermal >= 2 ? 11.0f : 22.0f;
    CrowdCull cull = {hot, lods, g->view_proj, g->player.kin.pos.X, g->player.kin.pos.Z, near_m * near_m, floor, g->no_lod};
    if (p->count >= 100) {
        jobs_run(app->jobs, crowd_cull_job, &cull, p->count, 32);
        jobs_wait(app->jobs);
    } else {
        crowd_cull_job(&cull, 0, p->count, 0);
    }
    const float blend = mg_minf(dt * 8.0f, 1.0f);
    bool queue_waiting = false; /* the receptionist nods at whoever is at the front */
    for (int i = 0; i < g->rooms.slot_count; i++) {
        const Slot *sl = &g->rooms.slots[i];
        queue_waiting = queue_waiting || ((sl->tags & SLOT_RECEPTION_QUEUE) && sl->order == 0 && sl->occupant);
    }
    for (int i = 0; i < p->count; i++) {
        if (lods[i] == 255 || (half && (p->cold[i].id & 1u))) {
            continue;
        }
        const npc_hot_t *h = &hot[i];
        npc_cold_t *c = &p->cold[i];
        const float w = mg_clampf((now - (float)h->last_tick - 1.0f) / (float)h->rate, 0.0f, 1.0f);
        HMM_Vec3 feet = HMM_V3(h->prev_x + (h->x - h->prev_x) * w, h->y, h->prev_z + (h->z - h->prev_z) * w);
        if (c->pose == POSE_SIT || c->pose == POSE_BED) { /* slide onto the seat */
            feet.X += (c->seat_x - feet.X) * c->sit_t;
            feet.Z += (c->seat_z - feet.Z) * c->sit_t;
        }
        /* reactions to the local player are drawn, never simulated: the sim must not depend on who is watching */
        const float px = me.X - feet.X, pz = me.Z - feet.Z, pd2 = px * px + pz * pz;
        const bool near_me = (h->storey < 0 ? 0 : (int)h->storey) == floor && pd2 < 2.5f * 2.5f && pd2 > 1e-4f;
        float off_x = 0.0f, off_z = 0.0f, yaw_add = 0.0f;
        const float spd2 = h->vx * h->vx + h->vz * h->vz;
        if (near_me && pd2 < 1.5f * 1.5f && spd2 > 0.01f) { /* steps aside */
            const float s_inv = 1.0f / sqrtf(spd2), ux = h->vx * s_inv, uz = h->vz * s_inv;
            if (px * ux + pz * uz > 0.0f) {
                const float side = (-uz * px + ux * pz) > 0.0f ? -1.0f : 1.0f, k = 0.5f * (1.0f - sqrtf(pd2) / 1.5f);
                off_x = -uz * side * k;
                off_z = ux * side * k;
                if (!(nav_flags_at(g->nav, h->storey < 0 ? 0 : h->storey, feet.X + off_x * 2.0f, feet.Z + off_z * 2.0f) & NAV_WALKABLE)) {
                    off_x = off_z = 0.0f; /* not into a wall */
                }
            }
        } else if (near_me && c->phase == NPC_PHASE_HOLD && c->pose == POSE_STAND) { /* greets the player */
            float d = atan2f(-px, -pz) - h->yaw;
            d = fmodf(d + 3.0f * HMM_PI32, 2.0f * HMM_PI32) - HMM_PI32;
            yaw_add = d;
        }
        c->vis_x += (off_x - c->vis_x) * blend;
        c->vis_z += (off_z - c->vis_z) * blend;
        c->vis_yaw += (yaw_add - c->vis_yaw) * blend;
        feet.X += c->vis_x;
        feet.Z += c->vis_z;
        AvatarPose pose = {feet, h->yaw + c->vis_yaw, HMM_V3(h->vx, 0.0f, h->vz), true, false, h->storey >= 0, false, true, lods[i]};
        pose.sit = c->pose == POSE_SIT ? c->sit_t : 0.0f;
        pose.lying = c->pose == POSE_BED ? c->sit_t : 0.0f;
        pose.crouch = c->pose == POSE_CROUCH ? 1.0f : 0.0f;
        pose.sway = c->pose == POSE_MOP ? 0.12f * HMM_SinF((float)app->time * 3.0f + (float)i) : 0.0f;
        pose.hide = c->fade < 1.0f ? 1.0f - c->fade : 0.0f;
        if (g->menu_world_on) { /* the hero stays the nearest thing: anyone coming closer fades out first */
            pose.hide = mg_maxf(pose.hide, mg_clampf((feet.Z - (MENU_HERO_Z - 1.0f)) / 1.5f, 0.0f, 1.0f));
        }
        npc_idle_touches(g, app, i, h, c, &pose, dt, queue_waiting);
        avatar_animate(&c->anim, &pose, dt);
        avatar_draw(app->gfx, &g->avatar_meshes, &c->look, &pose, &c->anim, NULL);
        g->rendered_entities++;
        if (g->settings.show_npc_paths && h->state == MOVE_WALK) {
            markers_draw_path(&g->markers, app->gfx, &c->path, h->next, feet, floor, 0xFFD166FFu, 1.0f);
        }
        if (g->settings.show_packages && (h->storey < 0 ? 0 : (int)h->storey) == floor) { /* debug only: name tags are for real players */
            char label[64];
            SDL_snprintf(label, sizeof(label), "%s %d.%d", g->npcs.data.archetypes[c->archetype].name, c->step, c->phase);
            name_tag(g, app, HMM_AddV3(feet, HMM_V3(0.0f, c->pose == POSE_BED ? 1.0f : 2.0f, 0.0f)), label);
        }
    }
}

/* debug: every slot of the view floor as an arrow; free green, taken red, blocked magenta;
 * the tag name shows within 3 m of the player */
static void draw_slots(Game *g, MgApp *app, int floor, HMM_Vec3 feet)
{
    for (int i = 0; i < g->rooms.slot_count; i++) {
        const Slot *sl = &g->rooms.slots[i];
        if ((sl->storey < 0 ? 0 : (int)sl->storey) != floor) {
            continue;
        }
        const float y = scene_floor_y(g->scene, floor) + 0.08f;
        HMM_Mat4 m = HMM_MulM4(HMM_Translate(HMM_V3(sl->x, y, sl->z)), HMM_Rotate_RH(sl->yaw, HMM_V3(0, 1, 0)));
        m = HMM_MulM4(m, HMM_Scale(HMM_V3(0.16f, 0.06f, 0.5f)));
        GfxMaterial mat = mat_unlit(!sl->valid ? HMM_V4(1, 0, 1, 1) : sl->occupant ? HMM_V4(1, 0.15f, 0.15f, 1) : HMM_V4(0.2f, 1, 0.3f, 1));
        gfx_draw(app->gfx, g->unit_box, &m, &mat, GFX_DRAW_NO_SHADOW);
        const float dx = sl->x - feet.X, dz = sl->z - feet.Z;
        if (dx * dx + dz * dz < 9.0f) {
            name_tag(g, app, HMM_V3(sl->x, y + 0.5f, sl->z), slot_tag_name(sl->tags));
        }
    }
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

static void game_frame_inner(MgApp *app, float alpha);

void game_frame(MgApp *app, float alpha)
{
    game_frame_inner(app, alpha);
    Game *g = (Game *)app->game;
    head_icons_update(&g->heads, app->gfx); /* what this frame's UI asked for */
}

static void game_frame_inner(MgApp *app, float alpha)
{
    Game *g = (Game *)app->game;
    g->frame_alpha = alpha;
    GameSettings *s = &g->settings;
    if (g->npcs_ok && g->playing) {
        /* other autotests keep the world empty, so their numbers stay comparable */
        const char *at_kind = arg_value(app->argc, app->argv, "--autotest");
        const bool roster_test = at_kind && (SDL_strcmp(at_kind, "friends") == 0 || SDL_strcmp(at_kind, "interact") == 0 || SDL_strcmp(at_kind, "npcs") == 0 || SDL_strcmp(at_kind, "npcs-save") == 0 || SDL_strcmp(at_kind, "npcs-continue") == 0 ||
                                             perf_wants_npcs(g));
        if (!at_kind || roster_test) {
            const uint32_t seed = npc_seed_for(g, app);
            const bool want = roster_test || s->npc_density > 0.0f; /* density Low only draws fewer: every client simulates the same people */
            if (want && (!g->npcs_on || seed != g->npcs.seed || s->respawn_npcs)) {
                s->respawn_npcs = false;
                g->npcs_on = true;
                npc_world_start(&g->npcs, seed);
                g->npc_catching_up = true; /* to the room's tick, or the save's */
            } else if (!want && g->npcs_on) {
                g->npcs_on = false;
                npc_world_stop(&g->npcs);
            }
        }
    } else if (g->npcs_ok && g->npcs_on && !g->menu_world_on) {
        g->npcs_on = false;
        npc_world_stop(&g->npcs);
    }
    if (g->npcs_on && g->playing) { /* the menu's backdrop has no room tick */
        npc_room_epoch(g);
    }
    npc_catch_up(g, app);
    npc_save_test_save(g, app);
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
    AutotestProbe probe = {&g->link, g->profile.guest, &g->stats, &g->friends, &g->lobby, &g->fpage, &g->notice, &g->heads, &g->profile.skin, g->profile.display_name, g->npc_sync_tick, g->npc_sync_hash,
                           g->player.kin.pos,
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
                           (int)g->stats.rides,
                           (float)g->stats.distance_m,
                           g->markers_reached,
                           g->active_marker,
                           g->touch.stick_touch >= 0,
                           g->hud_count,
                           g->hud_out,
                           g->playing && scene_indoors(g->scene, g->player.kin.pos)
                               ? scene_floor_of(g->scene, g->player.kin.pos)
                               : -1,
                           g->lift_panel_ok && g->lift_panel.slide > 0.5f,
                           g->ia_ok && g->panel.slide >= 1.0f && g->ia.active != IA_NONE,
                           g->ia_ok && g->ia.active != IA_NONE ? (int)g->ia.hot[g->ia.active].kind : -1,
                           g->panel.x + g->panel.size * 0.5f,
                           g->panel.y + g->panel.size * 0.5f};
    probe.show_stats = s->show_stats;
    probe.hud_res = g->hud_res;
    probe.hud_res_count = g->hud_res_count;
    if (g->text_test) {
        static const uint32_t ro[] = {0x103, 0xE2, 0xEE, 0x219, 0x21B, 0x102, 0xC2, 0xCE, 0x218, 0x21A};
        probe.text_ok = true;
        for (size_t i = 0; i < MG_ARRAY_COUNT(ro); i++) {
            probe.text_ok = probe.text_ok && text_has_glyph(&app->font, ro[i]);
        }
        /* the cedilla forms draw the comma glyphs */
        probe.text_ok = probe.text_ok && app->font.glyphs[0x15F].u0 == app->font.glyphs[0x219].u0 && app->font.glyphs[0x163].u0 == app->font.glyphs[0x21B].u0;
    }
    if (g->ia_check && g->ia_ok && g->playing) {
        interact_probe(g, app, &probe);
    } else {
        probe.active_slot = g->ia_ok ? (int)g->ia.active : -1;
    }
    probe.fe = &g->fe;
    AutotestOut ato;
    autotest_update(&g->autotest, app, &probe, &ato);
    {
        PerfSample ps;
        gfx_get_stats(app->gfx, &ps.gs);
        ps.sim = (uint32_t)(g->menu_world_on ? g->npcs.pool.count : g->crowd.pool.count) + 1;
        ps.rendered = g->rendered_last;
        ps.nav_bytes = g->nav ? nav_memory_bytes(g->nav) : 0;
        ps.npc_bytes = sizeof(npc_hot_t) * 2u * NPC_MAX + sizeof(npc_cold_t) * NPC_MAX;
        npc_autotest_check(g, app);
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
                                            "SETTINGS", "FRIENDS", "LOADING", "PLAY"};
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
        friends_tick(&g->friends, app->time);
        {
            const FeState st = g->fe.state;
            lobby_set_menu(&g->lobby, !g->playing && !g->play_paused &&
                                          (st == FE_MAIN_MENU || st == FE_SETTINGS || st == FE_FRIENDS || (st == FE_CREATOR && g->prefs.onboarding_done)));
        }
        lobby_tick(&g->lobby, app->time);
        lobby_follow(g, app);
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
            stats_loaded(&g->stats, &g->profile.stats, g->profile.user_id, app->time);
            session_set_identity(&g->session, g->profile.display_name, g->profile.skin);
            account_loaded(g, app);
        }
        g->stats.rooms = g->session.rooms_joined;
        if (stats_flush_due(&g->stats, app->time)) {
            NetStats t = stats_for_save(&g->stats);
            net_save_stats(g->net, &t);
        }
        g->local_look = session_my_look(&g->session);
        SDL_snprintf(g->status, sizeof(g->status), "%s", g->session.status_text);
    }
    if (app->input.key_pressed[SDL_SCANCODE_F1]) {
        g->fly_mode = !g->fly_mode;
    }
    if (app->input.key_pressed[SDL_SCANCODE_E] && g->ia_ok && g->playing && !g->menu.open) {
        interact_tap(g, app);
    }
    if (app->input.key_pressed[SDL_SCANCODE_F2]) {
        s->show_colliders = !s->show_colliders;
    }

    /* UI claims touches first (pause button + menu), then gameplay controls. The
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
    const bool stats_was = s->show_stats;
    MenuAction action = MENU_NONE;
    if (g->fpage.open) { /* opened from the pause card; its back arrow returns there */
        friends_page_draw(&g->fpage, &g->ui, &g->friends, &g->lobby, &g->heads, app, g->profile.display_name, &g->profile.skin);
    } else {
        action = settings_menu(&g->menu, &g->ui, s, &acct, app->width_pt, app->height_pt, app->safe_left, app->safe_top, app->safe_right,
                               app->safe_bottom, g->status);
    }
    if (s->show_stats != stats_was) {
        save_prefs(g); /* per device, and a kill must not lose it */
    }
    handle_menu_action(g, app, action);
    poll_sign_in(g, app);
    draw_notices(g, app);
    app_text_input(app, (g->menu.open && g->menu.editing_name) || friends_page_editing(&g->fpage));
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
    /* auto-nav rides on its own: no panel unless the player cancels */
    const bool menu_shown = el && !riding && !g->menu.open && !g->no_panels && (!autonav_active(&g->autonav) || s->force_lift_panel) &&
                            (elevator_contains(el, g->player.kin.pos, g->kparams.radius) || s->force_lift_panel);
    if (g->lift_panel_ok && el) {
        const ui_frame_t lf = hud_frame(app);
        elevator_panel_update(&g->lift_panel, mg_minf(app->frame_dt, 0.1f), menu_shown, &lf);
        const int due = elevator_panel_take(&g->lift_panel);
        if (due >= 0) {
            elevator_request(el, due);
        }
        for (int k = 0; menu_shown && k < 10 && k < el->floors; k++) { /* desktop: digits */
            if (app->input.key_pressed[k == 0 ? SDL_SCANCODE_0 : SDL_SCANCODE_1 + k - 1]) {
                lift_panel_press(g, el, k);
            }
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
        g->touch_cfg.radius_pt = 60.0f * k;
        g->touch_cfg.dead_zone_pt = s->dead_zone_pt * k;
        g->touch_cfg.jump_size_pt = PLAYER_JUMP_ENABLED ? 72.0f * k : 0.0f;
        touch_controls_update(&g->touch, &g->touch_cfg, &app->input, app->width_pt, app->height_pt, 0.0f, 0.0f, app->time);
        if (g->touch.tapped_id == PREVIEW_TAP_ID) {
            interact_tap(g, app);
        } else if (g->touch.tapped_id >= LIFT_PANEL_TAP_ID && g->touch.tapped_id < LIFT_PANEL_TAP_ID + LIFT_PANEL_FLOORS) {
            if (el) {
                lift_panel_press(g, el, (int)(g->touch.tapped_id - LIFT_PANEL_TAP_ID));
            }
        } else if (g->touch.tapped_id && g->nav) {
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
        if (at && SDL_strcmp(at, "npcs-bench") == 0 && g->npcs_ok) {
            run_npc_bench(g, app);
        }
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
        draw_ia_debug(g, app, floor);
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
    if (g->npcs_ok && g->npcs_on && g->npcs.pool.count > 0 && !g->npc_catching_up) {
        PROF_SCOPE("npc.roster_draw");
        draw_npcs(g, app, floor, alpha);
    }
    if (g->crowd_ok && g->crowd.pool.count > 0) {
        PROF_SCOPE("npc.draw");
        draw_crowd(g, app, floor, alpha);
    }
    if (s->show_slots) {
        draw_slots(g, app, floor, feet);
    }
    if (s->show_colliders) {
        draw_colliders(g, app->gfx, feet);
    }

    if (g->menu.open) {
        return;
    }
    if (g->nav) {
        markers_hud(g, app, floor, feet);
    }
    {
        const ui_frame_t cf = hud_frame(app);
        float cpx, cpy, cps;
        ui_pause_rect(&cf, &cpx, &cpy, &cps);
        head_chip_draw(&g->heads, app->gfx, &app->font, &g->profile.skin, g->profile.display_name, ui_safe_right(&cf) - UI_MARGIN - cf.corner, cpy);
    }
    g->touch.hide_hint = g->lift_panel_ok && g->lift_panel.slide > 0.0f;
    touch_controls_draw(&g->touch, &g->touch_cfg, app->gfx, &app->font, 0.0f, app->height_pt, 0.0f);
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
    if (s->show_stats && !g->perf.active) { /* perf numbers never include the overlay */
        hud_draw(app->gfx, &app->font, 10.0f, STATS_Y(0.0f, 0.0f), &hud);
    }
    if (s->show_safe_rect) {
        const ui_frame_t lf = frame_of(app);
        const UiColor c = ui_rgba(0xFF4DA6FF);
        ui_rect(app->gfx, &app->font, ui_safe_left(&lf), ui_safe_top(&lf), ui_safe_w(&lf), 1.0f, c);
        ui_rect(app->gfx, &app->font, ui_safe_left(&lf), ui_safe_bottom(&lf) - 1.0f, ui_safe_w(&lf), 1.0f, c);
        ui_rect(app->gfx, &app->font, ui_safe_left(&lf), ui_safe_top(&lf), 1.0f, ui_safe_h(&lf), c);
        ui_rect(app->gfx, &app->font, ui_safe_right(&lf) - 1.0f, ui_safe_top(&lf), 1.0f, ui_safe_h(&lf), c);
        char insets[64];
        SDL_snprintf(insets, sizeof(insets), "insets l%.0f t%.0f r%.0f b%.0f", (double)lf.inset_l, (double)lf.inset_t, (double)lf.inset_r, (double)lf.inset_b);
        text_draw_shadow(app->gfx, &app->font, ui_safe_left(&lf) + 10.0f, ui_safe_bottom(&lf) - 20.0f, 13.0f, c, insets);
    }
    layout_autotest(g, app);
    draw_chat_bubbles(g, app, floor);
    if (g->text_test) { /* --autotest text */
        const float tw = mg_minf(app->width_pt - 40.0f, 760.0f), tx = (app->width_pt - tw) * 0.5f, ty = app->height_pt * 0.5f - 70.0f;
        ui_round_rect(app->gfx, &app->font, tx, ty, tw, 140.0f, 14.0f, ui_rgba(0x101218F0));
        text_draw_centered(app->gfx, &app->font, tx + tw * 0.5f, ty + 44.0f, 30.0f, ui_rgba(0xFFFFFFFF), str("autotest_text"));
        text_draw_centered(app->gfx, &app->font, tx + tw * 0.5f, ty + 100.0f, 20.0f, ui_rgba(0xFFD166FF), str("autotest_text_line"));
    }
    if (g->greet_t > 0.0f) { /* bottom right, beside the preview card */
        g->greet_t = mg_maxf(g->greet_t - mg_minf(app->frame_dt, 0.1f), 0.0f);
        float gr[4];
        greet_rect(g, app, gr);
        const float cw = gr[2], ch = gr[3], cx = gr[0], cy = gr[1];
        gfx_ui_blur_panel(app->gfx, cx, cy, cw, ch, 12.0f, HMM_V4(0.06f, 0.07f, 0.09f, 0.6f));
        text_draw_centered(app->gfx, &app->font, cx + cw * 0.5f, cy + 18.0f, 13.0f, ui_rgba(0xFFD166FF), g->greet_name);
        text_draw_centered(app->gfx, &app->font, cx + cw * 0.5f, cy + 40.0f, 15.0f, ui_rgba(0xFFFFFFFF), g->greet_line);
    }
    if (g->lift_panel_ok && el) {
        elevator_panel_draw_3d(&g->lift_panel, app->gfx, el->car_floor);
    }
    if (g->ia_ok) {
        const ui_frame_t pf = hud_frame(app);
        const bool hide_panel = g->menu.open || riding || (g->lift_panel_ok && g->lift_panel.slide > 0.0f && !g->hud_clamp_test);
        preview_panel_update(&g->panel, hide_panel || g->no_panels ? &k_no_interact : &g->ia, mg_minf(app->frame_dt, 0.1f), &pf);
        preview_panel_draw_3d(&g->panel, &g->ia, app->gfx, &g->avatar_meshes);
        preview_panel_draw_ui(&g->panel, &g->ia, app->gfx, &app->font);
        if (preview_panel_visible(&g->panel) && g->settings.chat_bubbles != 0 && g->ia.hot[g->panel.shown].kind == IA_NPC &&
            (g->ia.hot[g->panel.shown].flags & IA_WANTS_CHAT)) {
            draw_cloud_at(app, g->panel.x + g->panel.size * 0.82f, g->panel.y + g->panel.size * 0.12f, true, 1.0f, 1.5f);
        }
    }
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
    draw_net_badge(g, app);
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
        g->marker_override = count ? SDL_atoi(count) : (at && SDL_strcmp(at, "markers") == 0 ? 8 : (g->perf.active || (at && SDL_strcmp(at, "interact") == 0) ? 0 : (at && SDL_strcmp(at, "hudclamp") == 0 ? 16 : -1)));
        if (at && SDL_strcmp(at, "interact") == 0) {
            marker_add(ms, HMM_V3(10.4f, scene_floor_y(g->scene, 0), -3.0f), 0, MARKER_OBJECTIVE, 0xFF9F0AFF);
        }
        if (one && SDL_sscanf(one, "%f,%f,%d", &x, &z, &st) == 3) {
            marker_add(ms, HMM_V3(x, scene_floor_y(g->scene, level_of(st)), z), st, MARKER_OBJECTIVE, 0xFF9F0AFF);
        }
    }
    markers_tick(ms, mg_minf(app->frame_dt, 0.1f));
    marker_t *a = marker_get(ms, g->active_marker);
    if (g->autonav.marker_id == 0) {
        /* a walk without a marker (desktop click, autotest) keeps its result */
    } else if (g->autonav.state == AUTONAV_ARRIVED) { /* stops at it; the panel is what fires it */
        if (a) {
            marker_set_state(a, MARKER_IDLE);
            marker_cue(g, AUDIO_CUE_TICK);
        }
        g->active_marker = 0;
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
    int alive_test = 0;
    for (int i = 0; i < MARKER_MAX; i++) {
        marker_t *m = &ms->m[i];
        if (!m->id || m->state == MARKER_REACHED) {
            continue;
        }
        alive_test += m->kind == MARKER_TEST;
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

static void markers_hud(Game *g, MgApp *app, int view_floor, HMM_Vec3 feet)
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
    /* panels the icons stay out of: pause button, lift panel, interact card, greeting card, toast */
    const float w = app->width_pt, h = app->height_pt;
    float *res = g->hud_res;
    int rc = 0;
    const ui_frame_t uf = hud_frame(app);
    float px, py, ps;
    ui_pause_rect(&uf, &px, &py, &ps);
    const float pause_btn[4] = {px - 6.0f, py - 6.0f, ps + 12.0f, ps + 12.0f};
    memcpy(res + rc * 4, pause_btn, sizeof(pause_btn));
    rc++;
    if (g->lift_panel_ok && elevator_panel_visible(&g->lift_panel)) {
        const float fm[4] = {g->lift_panel.x, g->lift_panel.y, g->lift_panel.w, g->lift_panel.h};
        memcpy(res + rc * 4, fm, sizeof(fm));
        rc++;
    }
    if (g->ia_ok && preview_panel_visible(&g->panel)) {
        const float card[4] = {g->panel.x, g->panel.y, g->panel.size, g->panel.size};
        memcpy(res + rc * 4, card, sizeof(card));
        rc++;
    }
    if (g->greet_t > 0.0f) {
        greet_rect(g, app, res + rc * 4);
        rc++;
    }
    {
        const float chip[4] = {ui_safe_right(&uf) - UI_MARGIN - uf.corner - CHIP_RESERVED_W, py, CHIP_RESERVED_W, 44.0f};
        memcpy(res + rc * 4, chip, sizeof(chip));
        rc++;
    }
    if (g->notice.count > 0 && g->notice.w > 0.0f) {
        const float nr[4] = {g->notice.x, g->notice.y, g->notice.w, g->notice.h};
        memcpy(res + rc * 4, nr, sizeof(nr));
        rc++;
    }
    if (g->settings.show_stats && !g->perf.active) { /* the same box --autotest layout checks */
        const float st[4] = {10.0f, STATS_Y(0.0f, 0.0f), 200.0f, 90.0f};
        memcpy(res + rc * 4, st, sizeof(st));
        rc++;
    }
    if (frontend_toast_active(&g->fe, app->time)) {
        const float t[4] = {w * 0.5f - 170.0f, h - mg_maxf(app->safe_bottom, 12.0f) - 60.0f, 340.0f, 48.0f};
        memcpy(res + rc * 4, t, sizeof(t));
        rc++;
    }
    g->hud_res_count = rc;
    MarkerHudView v = {g->view_proj, w, h, 0.0f, 0.0f, 0.0f, 0.0f, view_floor, res, rc};
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
            g->touch.targets[nt].half = 22.0f;
            nt++;
        }
    }
    if (g->lift_panel_ok && g->lift_panel.slide >= 1.0f) {
        for (int i = 0; i < g->lift_panel.floors && nt < TOUCH_MAX_TARGETS; i++) {
            float bx, by;
            elevator_panel_button_pos(&g->lift_panel, i, &bx, &by);
            g->touch.targets[nt].id = LIFT_PANEL_TAP_ID + (uint32_t)i;
            g->touch.targets[nt].x = bx;
            g->touch.targets[nt].y = by;
            g->touch.targets[nt].half = mg_maxf(26.0f, elevator_panel_button_radius(&g->lift_panel) + 4.0f); /* at least 52 pt */
            nt++;
        }
    }
    if (g->ia_ok && preview_panel_visible(&g->panel) && nt < TOUCH_MAX_TARGETS) {
        g->touch.targets[nt].id = PREVIEW_TAP_ID; /* tested first by the touch pass, tap-only */
        g->touch.targets[nt].x = g->panel.x + g->panel.size * 0.5f;
        g->touch.targets[nt].y = g->panel.y + g->panel.size * 0.5f;
        g->touch.targets[nt].half = g->panel.size * 0.5f;
        nt++;
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

/* --autotest npcs-bench: the real NPC sim at 44 and 200 wanderers, 1 thread against all of them */
static void run_npc_bench(Game *g, MgApp *app)
{
    enum { TICKS = 1800 };
    static const int sizes[2] = {44, 200};
    bool same = true;
    for (int s = 0; s < 2; s++) {
        double ms[2];
        uint32_t hash[2];
        int used[2];
        int threads[2] = {1, JOBS_MAX_THREADS};
        for (int run = 0; run < 2; run++) {
            jobs_limit_threads(app->jobs, threads[run]);
            used[run] = jobs_thread_count(app->jobs);
            npc_world_bench(&g->npcs, sizes[s], 0xC0FFEEu);
            g->npcs.always_jobs = true; /* measure the dispatch at both sizes */
            double t0 = mg_time_now();
            for (int t = 0; t < TICKS; t++) {
                size_t mark = arena_mark(&app->frame);
                npc_tick(&g->npcs, app->jobs, &app->frame, MG_TICK_DT);
                arena_reset_to(&app->frame, mark);
            }
            ms[run] = (mg_time_now() - t0) * 1000.0 / (double)TICKS;
            hash[run] = npc_world_hash(&g->npcs);
        }
        same = same && hash[0] == hash[1];
        mg_log_info("npc bench: %d NPCs: %d thread(s) %.3f ms per tick, %d thread(s) %.3f ms per tick, positions %s", g->npcs.pool.count, used[0], ms[0],
                    used[1], ms[1], hash[0] == hash[1] ? "identical" : "DIFFERENT");
    }
    npc_world_stop(&g->npcs);
    mg_log_info("AUTOTEST %s", same ? "PASSED" : "FAILED");
    app->quit_requested = true;
}

/* --autotest npcs, every second: the roster rules */
static void npc_autotest_check(Game *g, MgApp *app)
{
    static double next;
    static int min_alive = 1 << 20;
    if (!g->perf.active || SDL_strcmp(g->perf.scenario, "npcs") != 0 || !g->npcs_on || !g->perf.recording) {
        return;
    }
    const NpcWorld *w = &g->npcs;
    if (app->time >= next) {
        next = app->time + 1.0;
        const NpcPool *p = &w->pool;
        const npc_hot_t *hot = p->hot[p->read];
        min_alive = p->count < min_alive ? p->count : min_alive;
        static uint32_t last_hash, last_change_tick;
        const uint32_t now_hash = npc_world_hash(w);
        if (now_hash != last_hash) {
            last_hash = now_hash;
            last_change_tick = w->tick;
        } else if (w->tick - last_change_tick > 1200u && !g->perf.why_failed[0]) {
            SDL_snprintf(g->perf.why_failed, sizeof(g->perf.why_failed), "assert world_moving: tick %u, no NPC has moved since tick %u, seed %u", w->tick, last_change_tick, w->seed);
        }
        for (int i = 0; i < p->count && !g->perf.why_failed[0]; i++) {
            const npc_hot_t *h = &hot[i];
            const npc_cold_t *c = &p->cold[i];
            if (!(nav_flags_at(w->nav, h->storey < 0 ? 0 : h->storey, h->x, h->z) & NAV_WALKABLE)) {
                SDL_snprintf(g->perf.why_failed, sizeof(g->perf.why_failed), "assert on_grid: tick %u, npc %u (%s) at (%.2f, %.2f) storey %d, seed %u", w->tick, c->id, c->name, (double)h->x, (double)h->z,
                             h->storey, w->seed);
            }
            if (c->age == AGE_CHILD && c->phase != NPC_PHASE_FADE_OUT) { /* a leaving parent takes the child along */
                const int room = room_at(w->rooms, h->x, h->z, h->storey);
                const RoomType t = room >= 0 ? w->rooms->rooms[room].type : ROOM_YARD;
                if (npc_index_by_id(w, c->leader_id) < 0 || (t != ROOM_LOBBY && t != ROOM_WARD && t != ROOM_YARD)) {
                    SDL_snprintf(g->perf.why_failed, sizeof(g->perf.why_failed), "assert child_escorted: tick %u, npc %u (%s), room type %d, leader %u, seed %u", w->tick, c->id, c->name, (int)t, c->leader_id,
                                 w->seed);
                }
            }
        }
        if (min_alive < 44 && !g->perf.why_failed[0]) {
            SDL_snprintf(g->perf.why_failed, sizeof(g->perf.why_failed), "assert min_alive: tick %u, only %d alive, seed %u", w->tick, min_alive, w->seed);
        }
        if (w->stats.stuck_events > 0 && !g->perf.why_failed[0]) {
            SDL_snprintf(g->perf.why_failed, sizeof(g->perf.why_failed), "assert not_stuck: tick %u, npc %u stuck over 2 s (%u events), seed %u", w->stats.last_stuck_tick, w->stats.last_stuck_id,
                         w->stats.stuck_events, w->seed);
        }
    }
    SDL_snprintf(g->perf.extra_json, sizeof(g->perf.extra_json),
                 "\"npc\": {\"alive\": %d, \"min_alive\": %d, \"max_alive\": %u, \"spawned\": %u, \"despawned\": %u, \"stuck_events\": %u, \"path_requests\": %u}",
                 w->pool.count, min_alive, w->stats.max_alive, w->stats.spawned, w->stats.despawned, w->stats.stuck_events, w->stats.path_requests);
}

/* --autotest layout: nothing inside the area the OS excludes, nothing far from an edge the OS leaves free */
static void layout_autotest(Game *g, MgApp *app)
{
    const char *at = arg_value(app->argc, app->argv, "--autotest");
    if (!at || SDL_strcmp(at, "layout") != 0 || app->time < 3.0) {
        return;
    }
    const ui_frame_t f = hud_frame(app);
    float px, py, ps;
    ui_pause_rect(&f, &px, &py, &ps);
    static const char *const orient[5] = {"unknown", "landscape (SDL)", "landscape-flipped (SDL)", "portrait", "portrait-flipped"};
    const SDL_DisplayOrientation so = SDL_GetCurrentDisplayOrientation(SDL_GetPrimaryDisplay());
    mg_log_info("autotest layout: display orientation %s", orient[so >= 0 && so < 5 ? so : 0]);
    const float r = g->touch_cfg.radius_pt * 0.75f;
    const float stick_x = f.inset_l + 16.0f, stick_y = f.h - f.inset_b - 16.0f - 2.0f * r;
    const float nk = f.h >= 600.0f ? 1.3f : 1.0f; /* the join panel, ui/notice_panel.c */
    const struct { const char *name; float x, y, w, h; } items[] = {
        {"pause", px, py, ps, ps},
        {"stick", stick_x, stick_y, 2.0f * r, 2.0f * r},
        {"chip", ui_safe_right(&f) - UI_MARGIN - f.corner - CHIP_RESERVED_W, py, CHIP_RESERVED_W, 44.0f},
        {"stats", f.inset_l + 10.0f + f.corner, STATS_Y(f.inset_t, f.corner), 200.0f, 90.0f},
        {"notice", (f.w - 360.0f * nk) * 0.5f, f.inset_t + 8.0f, 360.0f * nk, 64.0f * nk},
    };
    bool ok = true;
    float left = 1e9f, right = 1e9f, top = 1e9f, bottom = 1e9f;
    for (size_t i = 0; i < MG_ARRAY_COUNT(items); i++) {
        const bool in = ui_rect_in_safe(&f, items[i].x, items[i].y, items[i].w, items[i].h);
        ok = ok && in;
        if (!in) {
            mg_log_error("autotest layout: assert in_safe_rect: %s at (%.0f, %.0f) size %.0f x %.0f, insets l%.0f t%.0f r%.0f b%.0f", items[i].name, (double)items[i].x, (double)items[i].y, (double)items[i].w,
                         (double)items[i].h, (double)f.inset_l, (double)f.inset_t, (double)f.inset_r, (double)f.inset_b);
        }
        left = mg_minf(left, items[i].x - ui_safe_left(&f));
        top = mg_minf(top, items[i].y - ui_safe_top(&f));
        right = mg_minf(right, ui_safe_right(&f) - items[i].x - items[i].w);
        bottom = mg_minf(bottom, ui_safe_bottom(&f) - items[i].y - items[i].h);
    }
    static const char *const edge[4] = {"left", "top", "right", "bottom"};
    const float gap[4] = {left, top, right, bottom}, inset[4] = {f.inset_l, f.inset_t, f.inset_r, f.inset_b};
    for (int e = 0; e < 4; e++) {
        const bool close = inset[e] > 0.0f || gap[e] <= 20.0f + f.corner;
        ok = ok && close;
        if (!close) {
            mg_log_error("autotest layout: assert uses_edge: %s edge has no inset but the nearest element is %.0f pt away", edge[e], (double)gap[e]);
        }
    }
    mg_log_info("autotest layout: %.0f x %.0f, insets l%.0f t%.0f r%.0f b%.0f, gaps l%.0f t%.0f r%.0f b%.0f", (double)f.w, (double)f.h, (double)f.inset_l, (double)f.inset_t, (double)f.inset_r, (double)f.inset_b,
                (double)left, (double)top, (double)right, (double)bottom);
    mg_log_info("AUTOTEST %s", ok ? "PASSED" : "FAILED");
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
    if (account_link_active(&g->link)) {
        return; /* these numbers may belong to the account that is being replaced */
    }
    if (g->stats.loaded) {
        NetStats t = stats_for_save(&g->stats);
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
    lobby_background(&g->lobby);
    net_set_background(g->net, true); /* leave the room: others see us go at once */
}

void game_resume(MgApp *app)
{
    Game *g = (Game *)app->game;
    net_set_background(g->net, false);
    lobby_foreground(&g->lobby);
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
        lobby_quit(&g->lobby);
        net_destroy(g->net); /* sends phx_leave before closing */
        g->net = NULL;
    }
    if (g) {
        audio_destroy(g->audio);
        g->audio = NULL;
    }
}
