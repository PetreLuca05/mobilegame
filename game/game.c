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
#include "book_people.h"
#include "interact.h"
#include "perf_run.h"
#include "preview_panel.h"
#include "rooms.h"
#include "hospital.h"
#include "platform/device_stats.h"
#include "stress.h"
#include "ui/layout.h"
#include "ui/prof_hud.h"
#include "avatar.h"
#include "avatar_gallery.h"
#include "menu_library.h"
#include "lib_move.h"
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
#include "content_cleanup.h"
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
#include "institute.h"
#include "scenery.h"
#include "settings.h"
#include "settings_menu.h"
#include "stats.h"
#include "ui/hud.h"
#include "ui/ui_textarea.h"
#include "book_pack.h"
#include "core/frame_notes.h"
#include "book_progress.h"
#include "book_check.h"
#include "missions.h"
#include "missions_ui.h"
#include "script_player.h"
#include "ui/haptics.h"
#include "ui/stick_view.h"
#include "ui/tilt.h"
#include "platform/motion.h"
#include "ui/touch_controls.h"
#include "ui/ui_widgets.h"
#include "world_data.h"
#include "worldmap.h"

typedef struct Game {
    Scene *scene;          /* the level scene built last (stays built after play until another kind is needed) */
    Arena level;           /* the level scene's memory: scene, nav, NPCs, interactables; reset on a scene switch */
    GfxMeshMark level_mesh_mark; /* GPU meshes from here up are the level's (gfx_mesh_reset_to on a switch) */
    bool play_common, play_common_ok; /* what every level shares is set up (once) */
    bool preview_ok;       /* the preview card's meshes (once) */
    int scene_override;    /* --scene hospital|institute|devroom (desktop), -1 = per level */
    int panel_lift;        /* the lift the 3D panel works (the one the player stands in), -1 none */
    int ride_lift;         /* the lift carrying the player while riding */
    int level_switch_test; /* --autotest levelswitch: ticks until the check (0 off), -1 done */
    double level_switch_end;
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
    StickView stick_view; /* UI-1b: the stick's look; reads touch, never writes it */
    Ui ui;
    SettingsMenu menu;
    AvatarAnim preview_anim;
    char account_msg[96];
    GfxMeshId unit_box;
    GfxMeshId ref_capsule; /* 1.8 m x 0.35 m player-sized ghost for the collider view */
    Scenery scenery;
    AvatarMeshes avatar_meshes;
    AvatarGallery gallery; /* --avatar-gallery NAME */
    MenuLibrary lib;       /* LIB-1: the menu's backdrop */
    bool lib_hero_walked;  /* the first onboarding screen's walk-in happened */
    bool lib_demo_join;    /* --lib-demo-join: a friend joins at 1 s and leaves at 10 s (no network) */
    double lib_demo_t0;
    int lib_demo_ctl;      /* --autotest library: the demo lobby by scenario (-1: the timeline above; 0 me alone, 1 + a friend, 2 a guest
                            * of hostA with mateB) */
    uint32_t plane_inv_seen, plane_req_seen; /* LIB-1 §8: invites / friend requests already turned into paper planes */
    char plane_to[NET_ID_LEN + 8];           /* a throw waiting for the menu (sent from the Friends page) */
    double plane_at;                         /* when it was asked for (0 none) */
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
    UiTest uitest; /* --autotest ui */
    Tilt tilt;      /* TILT-1: device tilt for the UI (visual only) */
    float tilt_s;   /* this tick's strength S */
    bool motion_on, tilt_debug, test_low_power, tilt_shake_pending, tilt_circle;
    float tilt_raw[3], tilt_screen[2]; /* Tilt debug: the last gravity, and after the rotation mapping */
    bool tilt_have, tilt_flipped;
    float tilt_shake_dir;
    float tilt_debug_x, tilt_debug_y;
    bool sys_reduce_motion, sys_reduce_logged;
    bool pause_was_open;
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
    float text_center_err, text_center_each[3]; /* --autotest text: label capitals vs face centre, pt */
    float hud_res[40];       /* what the marker icons stay out of: x, y, w, h (10 rects) */
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
    BookPeople bp;              /* BOOK-ENGINE S4b: the institute's people from the pack (g->npcs runs them; no roster) */
    bool bp_ok;
    uint32_t bp_drawn;          /* book people drawn this frame (all on the view storey) */
    int bp_drawn_floor;
    bool npc_catching_up, npc_sync_log;
    uint32_t npc_pose_log_tick; /* --log-npc-poses TICK: every NPC's pose once at that tick (0 off) */
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
    WorldBooks books;      /* WORLD-1: assets/world/books.json */
    WorldMap wm;           /* the island; loaded on first open */
    bool wm_tried;
    /* LIB-1 step 6: Maps / Back / New Game as one camera move */
    LibMove move;
    int move_state;        /* 0 none, 1 to the map, 2 back to the menu */
    double move_t0;
    bool move_entered;     /* the map's screen is up (t >= 0.9) */
    bool lib_placed;
    HMM_Vec3 last_eye;     /* the camera the last frame drew with (menu, move or map): tests */
    float last_cam_dist;   /* the move's camera distance to its target (tests) */
    bool inst_fill_1;      /* --debug inst-fill-1: the institute's fill lowered so sun + fill = 1.0 on an up face (not saved) */
    uint32_t lib_ab_frame; /* --lib-wall-ab */
    uint32_t fs_n;          /* --log-frame-stats */
    uint64_t fs_draws, fs_tris, fs_cat[GFX_CATS];
    uint32_t fs_vis[4];
    double fs_t0;
    bool lib_still;        /* --lib-still: the library at dt 0 (people stand where they start; comparison pictures)  */
    bool stale_eye_armed;  /* library test canary: one Back frame reports last frame's camera (the jump rule must catch it) */
    double stale_eye_until; /* the canary's stale camera holds until then */
    bool move_from_pause;  /* the map came from a hospital's pause card: Back keeps the scene switch */
    double dissolve_t0;    /* Reduced motion: the 300 ms cross-dissolve's start (0 none) */
    bool force_reduced;    /* --ui-motion reduced (not saved) */
    bool force_full;       /* perf menu_to_map: the move, whatever the profile says (not saved) */
    int move_demo;         /* --lib-move-demo: Maps at 3 s, Back at 9 s (pictures); 1 armed, 2 out, 3 back; 4 perf menu_to_map (looped) */
    struct {               /* --lib-back-loop N: Maps + Back N times, every Back's camera checked (the library test's jump rule) */
        int want, done, bad, frame;
        HMM_Vec3 prev_eye;
        float prev_t, prev_speed, worst;
    } back_loop;
    int play_book, play_level; /* this game's island and level (the save's) */
    char maps_tag[48];     /* "Kumar 23%", recomputed when the map opens and when a hospital is left */
    bool keep_save_pos;    /* Continue: ignore the room's first spawn */
    UiAnim ui_anim;
    int ui_screen_key; /* which screen the UI layer shows (its enter starts on a change) */
    int juice_cycles, juice_shown; /* perf scenario ui_juice */
    double juice_banner_at;
    double ui_screen_t0;        /* UI-1: the tween pool */
    UiFx ui_fx;            /* UI-1: particles, rings, reward tokens, banner, sounds, haptics */
    BookPack book;         /* BOOK-1: the embedded content pack, read in place (ok = false: refused, no book content) */
    BookProgress progress; /* BOOK-ENGINE: the account's facts + case state for the pack (book_progress_*) */
    char progress_uid[64]; /* whose file is open ("" none) */
    UiTextArea book_area;  /* --autotest text: a long text from the pack in a scrolling text area */
    TextLine *book_lines;
    char *book_text;
    uint32_t book_area_inst; /* UI instances the text area emitted last frame */
    /* BOOK-ENGINE S4: missions (on in a level whose scene is the institute and a pack with missions) */
    Missions missions;
    MissionUi mui;
    BookPeople *people;    /* the pack's people (level arena) */
    uint32_t mission_marker; /* the objective marker, 0 none */
    BookCheck bcheck;      /* the load-time data check of the last institute load */
    ScriptPlayer sp;       /* BOOK-ENGINE S5: the script player (a modal card over play) */
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
static void missions_setup(Game *g, MgApp *app);
static bool book_modal(const Game *g);
static void missions_frame(Game *g, MgApp *app);
static void missions_overlays(Game *g, MgApp *app);
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
    return g->perf.active && (SDL_strcmp(g->perf.scenario, "npcs") == 0 || SDL_strcmp(g->perf.scenario, "interact_npc") == 0 || SDL_strcmp(g->perf.scenario, "menu") == 0 ||
                              SDL_strcmp(g->perf.scenario, "ui_juice") == 0);
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

#define LEVEL_ARENA_BYTES (64u << 20) /* the hospital: 38 MB peak while building (mesh builders' scratch), 9.6 MB kept; untouched pages cost nothing */

static void finish_map_preload(Game *g, MgApp *app);

/* Once, before the first level scene: what every level shares goes under the level mark (markers, the preview
 * card, the world map's meshes: the map preloads behind the menu, but a direct start into play has not finished
 * it; its meshes must not land above the mark or a scene switch would drop them). Then the level arena and mark. */
static bool ensure_play_common(Game *g, MgApp *app)
{
    if (g->play_common) {
        return g->play_common_ok;
    }
    g->play_common = true;
    finish_map_preload(g, app);
    g->preview_ok = preview_panel_init(&g->panel, app->gfx, &app->perm);
    g->all_paths = ARENA_PUSH(&app->perm, NavPath, MARKER_MAX);
    if (!markers_init(&g->markers, app->gfx, &app->perm) || !g->all_paths || !arena_sub_reserve(&app->perm, &g->level, LEVEL_ARENA_BYTES)) {
        mg_log_error("level: no room for the level arena (%u MB; perm %zu / %zu used)", LEVEL_ARENA_BYTES >> 20, app->perm.used, app->perm.size);
        return false;
    }
    gfx_settle_uploads(app->gfx);
    g->level_mesh_mark = gfx_mesh_mark(app->gfx);
    mg_log_info("level: arena %u MB reserved, perm %zu / %zu MB used", LEVEL_ARENA_BYTES >> 20, app->perm.used >> 20, app->perm.size >> 20);
    g->play_common_ok = true;
    return true;
}

/* Everything that points into the level arena or at the level's meshes goes; the arena and the meshes above the
 * mark are freed. The shared parts (markers' meshes, the preview card, the world map) stay. */
static void unload_level(Game *g, MgApp *app)
{
    if (!g->scene) {
        return;
    }
    const double t0 = (double)SDL_GetTicksNS() * 1e-6;
    const char *name = scene_name(g->scene);
    missions_shutdown(&g->missions); /* BOOK-ENGINE S4: its people and player live in the level arena */
    g->people = NULL;
    g->mission_marker = 0;
    g->mui.journal = false;
    g->menu.journal_row = false;
    if (g->npcs_on) {
        npc_world_stop(&g->npcs);
    }
    g->npcs_on = g->npcs_ok = g->npc_catching_up = false;
    SDL_zero(g->npcs);
    SDL_zero(g->bp);
    g->bp_ok = false;
    g->crowd_ok = false;
    g->crowd.pool.count = 0; /* its requests went with the nav */
    autonav_clear(&g->autonav);
    g->active_marker = 0;
    SDL_memset(g->markers.m, 0, sizeof(g->markers.m)); /* every marker is a spot in the old scene */
    g->nav_path_ok = false;
    SDL_zero(g->nav_debug); /* its grids are level meshes */
    g->ia_ok = g->lift_panel_ok = false;
    g->panel.shown = IA_NONE;
    g->panel.slide = 0.0f;
    g->panel_lift = g->ride_lift = -1;
    g->riding = false;
    g->scene = NULL;
    g->nav = NULL;
    colworld_clear(&g->world);
    rooms_clear(&g->rooms);
    uint32_t meshes, vb, ib;
    gfx_mesh_usage(app->gfx, &meshes, &vb, &ib);
    gfx_mesh_reset_to(app->gfx, g->level_mesh_mark);
    const size_t peak = g->level.peak;
    arena_reset(&g->level);
    g->level.peak = 0;
    arena_release_unused(&g->level);
    mg_log_info("level: unloaded %s in %.1f ms: %u meshes, %u KB vertices, %u KB indices, arena peak %zu KB", name,
                (double)SDL_GetTicksNS() * 1e-6 - t0, meshes - g->level_mesh_mark.meshes, (vb - g->level_mesh_mark.vb_bytes) >> 10,
                (ib - g->level_mesh_mark.ib_bytes) >> 10, peak >> 10);
}

/* The level scene of `kind` (+ terrain, nav, room graph, NPC roster, interactables, lift panel), built into the
 * level arena and above the level mesh mark. A scene of another kind is unloaded first; the same kind stays as it
 * is (Continue / Join / back to the menu and in again). False if the scene cannot be built. */
static bool ensure_world(Game *g, MgApp *app, SceneKind kind)
{
    if (g->scene && scene_kind(g->scene) == kind) {
        return true;
    }
    frame_note("level build");
    if (!ensure_play_common(g, app)) {
        return false;
    }
    unload_level(g, app);
    const double t0 = (double)SDL_GetTicksNS() * 1e-6;
    colworld_clear(&g->world);
    g->panel_lift = g->ride_lift = -1;
    g->scene = scene_create(kind, app->gfx, &g->level, &g->world, &app->font);
    if (g->scene) {
        g->nav = scene_nav_bake(g->scene, &g->level, &g->world);
        g->crowd_ok = g->nav && stress_init(&g->crowd, &g->level);
        if (g->nav) {
            mg_log_info("nav: baked %d grids in %.1f ms", nav_grid_count(g->nav), (double)nav_bake_ms(g->nav));
        }
        if (g->nav) {
            scene_rooms_build(g->scene, &g->rooms);
            int bad = rooms_validate(&g->rooms, g->nav);
            mg_log_info("rooms: %d rooms, %d doors, %d slots, %d on blocked cells", g->rooms.room_count, g->rooms.door_count,
                        g->rooms.slot_count, bad);
            g->npcs_ok = scene_ambient_npcs(g->scene) && npc_world_init(&g->npcs, &g->level, &g->rooms, g->nav);
            if (!scene_ambient_npcs(g->scene)) {
                mg_log_info("npc: no ambient roster in %s", scene_name(g->scene));
            }
            if (kind == SCENE_INSTITUTE && g->book.ok && g->book.person_count > 0) { /* BOOK-ENGINE S4b: the pack's people instead */
                float fy[BOOK_PEOPLE_FLOORS];
                const int nf = SDL_min(scene_floor_count(g->scene), BOOK_PEOPLE_FLOORS);
                for (int f = 0; f < nf; f++) {
                    fy[f] = scene_floor_y(g->scene, f);
                }
                g->bp_ok = npc_world_init(&g->npcs, &g->level, &g->rooms, g->nav) &&
                           book_people_init(&g->bp, &g->level, &g->book, &g->npcs, &g->rooms, g->nav, fy, nf);
                g->npcs_ok = g->bp_ok;
            }
            interact_setup(g, app);
            missions_setup(g, app);
        }
        { /* Developer -> Teleport to floor: one button per floor of this scene */
            const int nf = SDL_min(scene_floor_count(g->scene), 8);
            g->menu.tp_floors = nf;
            for (int f = 0; f < nf; f++) {
                const char *fname = scene_floor_name(g->scene, f);
                SDL_snprintf(g->menu.tp_label[f], sizeof(g->menu.tp_label[f]), "%c##tp", fname && fname[0] ? fname[0] : '?');
            }
        }
    }
    SceneryShape shape = g->scene ? scene_scenery_shape(g->scene) : (SceneryShape){0};
    const bool ok = g->scene && scenery_init(&g->scenery, &shape, app->gfx, &g->level);
    gfx_settle_uploads(app->gfx);
    uint32_t meshes, vb, ib;
    gfx_mesh_usage(app->gfx, &meshes, &vb, &ib);
    mg_log_info("level: built %s in %.1f ms: %u meshes (%u KB vertices, %u KB indices) over the mark, arena %zu KB kept, peak %zu KB of %u MB, lifts %d, floors %d",
                g->scene ? scene_name(g->scene) : scene_kind_id(kind), (double)SDL_GetTicksNS() * 1e-6 - t0, meshes - g->level_mesh_mark.meshes,
                (vb - g->level_mesh_mark.vb_bytes) >> 10, (ib - g->level_mesh_mark.ib_bytes) >> 10, g->level.used >> 10, g->level.peak >> 10,
                LEVEL_ARENA_BYTES >> 20, g->scene ? scene_elevator_count(g->scene) : 0, g->scene ? scene_floor_count(g->scene) : 0);
    arena_release_unused(&g->level); /* the mesh builders' scratch */
    if (!ok) {
        unload_level(g, app);
    }
    return ok;
}

/* The scene a level loads: --scene, else the level's books.json "scene" (MG_SCENE=devroom: always the dev room). */
static SceneKind level_scene_kind(const Game *g, int book, int level)
{
    if (scene_kind_forced()) {
        return scene_default_kind();
    }
    if (g->scene_override >= 0) {
        return (SceneKind)g->scene_override;
    }
    if (book < 0 || book >= g->books.count || level < 0 || level >= g->books.books[book].level_count) {
        return scene_default_kind();
    }
    return scene_kind_from_id(g->books.books[book].levels[level].scene);
}

/* ------------------------------------------------------------ lifts (N per scene) */

static int lift_count(const Game *g) { return g->scene ? scene_elevator_count(g->scene) : 0; }
static Elevator *lift_get(Game *g, int i) { return g->scene && i >= 0 ? scene_elevator(g->scene, i) : NULL; }
static bool lift_holds_xz(const Elevator *e, HMM_Vec3 p) { return p.X >= e->x0 && p.X <= e->x1 && p.Z >= e->z0 && p.Z <= e->z1; }

/* The lift whose car the capsule stands in (elevator_contains), -1 none. */
static int lift_containing(Game *g, HMM_Vec3 feet, float radius)
{
    for (int i = 0; i < lift_count(g); i++) {
        if (elevator_contains(lift_get(g, i), feet, radius)) {
            return i;
        }
    }
    return -1;
}

/* The nearest car (to its centre, on the ground plan), -1 if the scene has no lift. */
static int lift_nearest(Game *g, HMM_Vec3 p)
{
    int best = -1;
    float best_d = 1e30f;
    for (int i = 0; i < lift_count(g); i++) {
        const Elevator *e = lift_get(g, i);
        const float dx = (e->x0 + e->x1) * 0.5f - p.X, dz = (e->z0 + e->z1) * 0.5f - p.Z, d = dx * dx + dz * dz;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

/* The player's own ride: the car they stand in is busy (closing, moving, opening), or they are being carried. Another car
 * moving (called from a landing, or a remote rider's) must not lock the player's input. */
static bool player_lift_busy(Game *g)
{
    if (g->riding) {
        return true;
    }
    const int i = lift_containing(g, g->player.kin.pos, g->kparams.radius);
    return i >= 0 && elevator_busy(lift_get(g, i));
}

static bool any_lift_busy(Game *g)
{
    for (int i = 0; i < lift_count(g); i++) {
        if (elevator_busy(lift_get(g, i))) {
            return true;
        }
    }
    return false;
}

/* Every car waits on `floor` (Continue, debug teleport); a lift without that landing keeps its place. */
static void lifts_place(Game *g, int floor)
{
    for (int i = 0; i < lift_count(g); i++) {
        Elevator *e = lift_get(g, i);
        if (floor < e->floors) {
            elevator_place(e, floor);
        }
    }
}

/* The landing call (MAP-2: with several lifts a car stays where it was last used): walking up to an idle car's landing on
 * another floor brings it there, doors opening on arrival, as a call button would. The landing zone is the car's plan
 * grown by LIFT_CALL_REACH on the player's storey; a player riding any car calls nothing. */
#define LIFT_CALL_REACH 1.6f
static void lifts_call(Game *g, HMM_Vec3 feet)
{
    if (any_lift_busy(g) && g->riding) {
        return;
    }
    for (int i = 0; i < lift_count(g); i++) {
        Elevator *e = lift_get(g, i);
        const int f = (int)lroundf(feet.Y / e->storey);
        if (elevator_busy(e) || f < 0 || f >= e->floors || f == e->car_floor || SDL_fabsf(feet.Y - (float)f * e->storey) > 0.6f) {
            continue;
        }
        if (feet.X > e->x0 - LIFT_CALL_REACH && feet.X < e->x1 + LIFT_CALL_REACH && feet.Z > e->z0 - LIFT_CALL_REACH && feet.Z < e->z1 + LIFT_CALL_REACH) {
            elevator_request(e, f);
        }
    }
}

/* The marker HUD's badge for a floor: the first letter of its name (G 1 2 .. R). */
static void floor_badge(const Game *g, int floor, char out[4])
{
    const char *n = g->scene ? scene_floor_name(g->scene, floor) : NULL;
    out[0] = n && n[0] ? n[0] : 0;
    out[1] = 0;
}

/* in play: the account chip top left, the pause button top right; the perf text goes under the chip */
#define CHIP_H 44.0f
#define STATS_Y(top, corner) ((top) + 8.0f + (corner) + CHIP_H + 8.0f)
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

static void progress_merged(Game *g);

static void on_net_event(void *user, const NetEvent *e)
{
    Game *g = (Game *)user;
    if (e->type == NET_EV_BOOK_PROGRESS) {
        progress_merged(g);
    }
    friends_on_event(&g->friends, e);
    lobby_on_event(&g->lobby, e);
    account_link_on_event(&g->link, e);
    if (e->type == NET_EV_PUSH && e->push.type == NET_PUSH_FRIEND_REQUEST) {
        notice_push(&g->notice, NOTICE_FRIEND, e->push.from, e->push.name, &e->push.skin, str("notice_friend_request"), NOTICE_FRIEND_S);
    }
}

static double wall_clock_ms(void);
static void maps_tag_update(Game *g);

/* BOOK-ENGINE progress (decision 4): one file per account in the pref path, the pack's content id as the book */
#define PROGRESS_BOOK "gastro"         /* book_progress.book on Supabase */
#define PROGRESS_WORLD_BOOK "kumar"    /* where the pack's level sits on the world map */
#define PROGRESS_WORLD_LEVEL "gastro"

static void progress_live(Game *g)
{
    if (g->progress_uid[0]) {
        world_progress_set_live(&g->prefs.world, PROGRESS_WORLD_BOOK, PROGRESS_WORLD_LEVEL,
                                (WorldCompletion){book_progress_count(&g->progress), (int)g->book.fact_count});
    }
}

/* Opens the account's file (uid "" = no account yet: a "local" file); same account = nothing. */
static void progress_open(Game *g, const char *uid)
{
    const char *who = uid && uid[0] ? uid : "local";
    if (!g->book.ok || SDL_strcmp(g->progress_uid, who) == 0) {
        return;
    }
    char name[128], path[512];
    SDL_snprintf(name, sizeof(name), "book_%s_%s.bin", PROGRESS_BOOK, who);
    if (!prefs_path(g->profile_name, name, path, sizeof(path)) ||
        !book_progress_open(&g->progress, path, PROGRESS_BOOK, (int)g->book.fact_count, (int)g->book.mission_count)) {
        mg_log_error("book: progress file for %s not opened", who);
        g->progress_uid[0] = 0;
        return;
    }
    SDL_snprintf(g->progress_uid, sizeof(g->progress_uid), "%s", who);
    mg_log_info("book: progress %s: %d / %u facts", name, book_progress_count(&g->progress), g->book.fact_count);
    progress_live(g);
}

/* A step ended (or the account loaded): the file now, the server merge when online. */
static void progress_commit(Game *g, bool push)
{
    if (!g->progress_uid[0]) {
        return;
    }
    if (g->progress.dirty) {
        book_progress_save(&g->progress);
    }
    if (push && g->net && SDL_strcmp(g->progress_uid, "local") != 0) {
        net_save_book_progress(g->net, PROGRESS_BOOK, g->progress.facts, g->progress.fact_bytes, g->progress.cases, g->progress.case_bytes,
                               g->progress.updated_ms);
    }
    progress_live(g);
}

/* The server's merged row (another device's bits OR'd in, the newer case state). */
static void progress_merged(Game *g)
{
    char book[32];
    uint8_t facts[NET_PROGRESS_FACTS_MAX], cases[NET_PROGRESS_CASES_MAX];
    int nf = 0, nc = 0;
    double at = 0.0;
    if (!net_book_progress_copy(g->net, book, sizeof(book), facts, &nf, cases, &nc, &at) || !g->progress_uid[0] ||
        SDL_strcmp(book, PROGRESS_BOOK) != 0) {
        return;
    }
    const int before = book_progress_count(&g->progress);
    book_progress_merge(&g->progress, facts, nf, cases, nc, at);
    progress_commit(g, false);
    mg_log_info("book: progress merged with the account: %d -> %d facts", before, book_progress_count(&g->progress));
    maps_tag_update(g);
}
static bool open_map(Game *g, MgApp *app, WmState state);
static void erase_progress(Game *g);

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
    avatar_animate(&g->avatar_meshes, &anim, &pose, 1.0f / 60.0f);
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

/* --autotest onboarding without --profile: its own profile, wiped first, so every run is a new install */
#define ONBOARDING_PROFILE "autotest-onboarding"

static SDL_EnumerationResult wipe_one(void *user, const char *dir, const char *name)
{
    (void)user;
    if (SDL_strncmp(name, ONBOARDING_PROFILE "-", sizeof(ONBOARDING_PROFILE)) == 0 || SDL_strstr(name, "." ONBOARDING_PROFILE ".")) { /* prefs, save; the guest token */
        char path[1024];
        SDL_snprintf(path, sizeof(path), "%s%s", dir, name);
        SDL_RemovePath(path);
    }
    return SDL_ENUM_CONTINUE;
}

bool game_init(MgApp *app)
{
    Game *g = ARENA_PUSH(&app->perm, Game, 1);
    app->game = g;
    const char *profile = arg_value(app->argc, app->argv, "--profile");
    const char *at_first = arg_value(app->argc, app->argv, "--autotest");
    if (!profile && at_first && SDL_strcmp(at_first, "onboarding") == 0) {
        char *dir = SDL_GetPrefPath("docvex", "mobilegame");
        if (dir) {
            SDL_EnumerateDirectory(dir, wipe_one, NULL);
            SDL_free(dir);
        }
        profile = ONBOARDING_PROFILE;
        mg_log_info("autotest onboarding: a fresh profile (%s, wiped)", profile);
    }
    SDL_snprintf(g->profile_name, sizeof(g->profile_name), "%s", profile ? profile : "");
    if (!strings_init(&app->perm) || !frontend_init(&g->fe, app->gfx, &app->perm)) {
        return false;
    }
    kinematic_default_params(&g->kparams);
    prefs_load(&g->prefs, g->profile_name);
    g->settings = g->prefs.settings; /* per device */
    ui_haptic_set_level((HapticLevel)g->settings.haptics);
    ui_haptics_init(); /* Core Haptics players are built off this thread: launch time does not change */
    if (!g->prefs.content_cleaned) { /* once: book files from builds that carried the pack (save older than v8) */
        content_cleanup_run(&g->prefs, g->profile_name);
        prefs_save(&g->prefs, g->profile_name);
    }
    g->has_save = save_exists(g->profile_name);
    book_pack_load(&g->book, arg_value(app->argc, app->argv, "--content")); /* none in the repository: no book content; tests: --content DIR */
    script_player_init(&g->sp, app->gfx); /* BOOK-ENGINE: the figures embedded, the GPU texture for the largest reserved */
    script_player_survey(&g->sp, &g->book, &app->font, &app->font_bold); /* the worst case per item type, logged as numbers */
    {
        const char *at = arg_value(app->argc, app->argv, "--autotest");
        g->sp.autoplay = at && (SDL_strcmp(at, "missions") == 0 || SDL_strcmp(at, "book") == 0); /* answered by the pack's key */
    }
    if (!world_books_load(&g->books)) {
        mg_log_error("world: assets/world/books.json has no book");
        return false;
    }
    progress_open(g, g->prefs.cache_user_id); /* the cached account until the sign-in confirms it */
    for (int i = 1; i + 2 < app->argc; i++) { /* --debug book-fact <i>: sets one fact as a finished step would (test builds) */
        if (SDL_strcmp(app->argv[i], "--debug") == 0 && SDL_strcmp(app->argv[i + 1], "book-fact") == 0 && g->progress_uid[0]) {
            book_progress_set_fact(&g->progress, SDL_atoi(app->argv[i + 2]), wall_clock_ms());
            progress_commit(g, false); /* pushed once the account loads */
        }
    }
    for (int i = 1; i + 3 < app->argc; i++) { /* --debug level-progress <level> <percent> (test builds) */
        if (SDL_strcmp(app->argv[i], "--debug") == 0 && SDL_strcmp(app->argv[i + 1], "level-progress") == 0) {
            world_debug_progress(&g->prefs.world, app->argv[i + 2], SDL_atoi(app->argv[i + 3]));
        }
    }
    tilt_init(&g->tilt);
    g->tilt_circle = has_flag(app->argc, app->argv, "--tilt-sine");
    g->settings.show_tilt_debug = has_flag(app->argc, app->argv, "--tilt-debug"); /* desktop: the overlay on */
    for (int i = 1; i + 3 < app->argc; i++) { /* --debug tilt <tx> <ty>: the tilt target, no sensor */
        if (SDL_strcmp(app->argv[i], "--debug") == 0 && SDL_strcmp(app->argv[i + 1], "tilt") == 0) {
            g->tilt_debug = true;
            g->tilt_debug_x = (float)SDL_atof(app->argv[i + 2]), g->tilt_debug_y = (float)SDL_atof(app->argv[i + 3]);
        }
    }
    for (int i = 1; i + 1 < app->argc; i++) { /* --debug inst-fill-1: test only, default off, not saved */
        g->inst_fill_1 |= SDL_strcmp(app->argv[i], "--debug") == 0 && SDL_strcmp(app->argv[i + 1], "inst-fill-1") == 0;
    }
    for (int i = 1; i + 2 < app->argc; i++) { /* --debug ride <plane|helicopter|boat|submarine>: the next trip */
        if (SDL_strcmp(app->argv[i], "--debug") == 0 && SDL_strcmp(app->argv[i + 1], "ride") == 0) {
            g->prefs.world.rides.forced = world_ride_from_name(app->argv[i + 2]);
        }
    }
    maps_tag_update(g);
    touch_controls_default_config(&g->touch_cfg);
    stick_view_init(&g->stick_view);
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
    g->gallery.mode = avatar_gallery_parse(arg_value(app->argc, app->argv, "--avatar-gallery"), &g->gallery.row);
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
    if (!menu_library_init(&g->lib, app->gfx, &app->perm)) {
        return false; /* LIB-1: the menu has no other backdrop */
    }
    gfx_settle_uploads(app->gfx); /* the library's meshes are in */
    for (int i = 1; i + 1 < app->argc; i++) { /* --debug turn-page: the hero turns a page every 2.5 s */
        g->lib.debug_turns |= SDL_strcmp(app->argv[i], "--debug") == 0 && SDL_strcmp(app->argv[i + 1], "turn-page") == 0;
        /* --debug fidget <name>: every seated reader plays it each time it is free; --debug phone-place <table|pocket> (LIB-1 §15) */
        if (SDL_strcmp(app->argv[i], "--debug") == 0 && SDL_strcmp(app->argv[i + 1], "fidget") == 0 && i + 2 < app->argc) {
            g->lib.debug_fidget = lib_action_from_name(app->argv[i + 2]);
        }
        if (SDL_strcmp(app->argv[i], "--debug") == 0 && SDL_strcmp(app->argv[i + 1], "phone-place") == 0 && i + 2 < app->argc) {
            menu_library_phone_place(&g->lib, SDL_strcmp(app->argv[i + 2], "table") == 0);
        }
    }
    {
        const char *cu = arg_value(app->argc, app->argv, "--lib-closeup");
        const char *at = arg_value(app->argc, app->argv, "--autotest");
        g->move_demo = has_flag(app->argc, app->argv, "--lib-move-demo") ? 1 : 0;
        if (arg_value(app->argc, app->argv, "--ui-motion") && SDL_strcmp(arg_value(app->argc, app->argv, "--ui-motion"), "reduced") == 0) {
            g->force_reduced = true; /* tests: Animații interfață = Reduse for this run, never written to prefs */
        }
        g->lib_demo_join = has_flag(app->argc, app->argv, "--lib-demo-join") || (at && SDL_strcmp(at, "library") == 0);
        g->lib_demo_ctl = -1;
        g->lib.closeup = has_flag(app->argc, app->argv, "--lib-closeup") ? (cu && SDL_strcmp(cu, "side") == 0 ? 2 : 1) : 0;
        if (arg_value(app->argc, app->argv, "--lib-bay")) { /* desktop: my table in another bay of the hall (pictures) */
            menu_library_set_table(&g->lib, SDL_atoi(arg_value(app->argc, app->argv, "--lib-bay")));
        }
        g->lib.no_nav = has_flag(app->argc, app->argv, "--lib-no-nav"); /* perf A/B: the straight-leg runner */
        g->lib.no_wall_vis = has_flag(app->argc, app->argv, "--lib-no-wall-vis"); /* A/B: bay visibility by the frustum alone */
        g->lib_still = has_flag(app->argc, app->argv, "--lib-still");
        g->lib.snap_camera = g->lib_still;
        g->lib.log_acts = has_flag(app->argc, app->argv, "--lib-log-acts"); /* every background person's act as it starts (pictures) */
        if (arg_value(app->argc, app->argv, "--lib-look")) { /* desktop pictures: the menu camera on hall point x,z from d m */
            float x = 0.0f, z = 0.0f, d = 0.0f;
            if (SDL_sscanf(arg_value(app->argc, app->argv, "--lib-look"), "%f,%f,%f", &x, &z, &d) == 3 && d > 0.0f) {
                g->lib.look_at = HMM_V2(x, z), g->lib.look_dist = d;
            }
        }
        if (arg_value(app->argc, app->argv, "--lib-lod-fade")) { /* perf: one NPC always mid-crossfade */
            g->lib.lod_fade_test = SDL_atoi(arg_value(app->argc, app->argv, "--lib-lod-fade"));
        }
        if (arg_value(app->argc, app->argv, "--lib-back-loop")) { /* Maps + Back N times, every Back checked */
            g->back_loop.want = SDL_atoi(arg_value(app->argc, app->argv, "--lib-back-loop"));
            g->move_demo = 4;
            g->force_full = true;
        }
        if (arg_value(app->argc, app->argv, "--lib-guest")) { /* desktop: a second player seated at another bay's table (pictures) */
            const AvatarLook guest = avatar_look_from_skin((NetSkin){.skin = 2, .hair = HAIR_SHORT, .hair_color = 3, .suit = 5});
            menu_library_add_guest(&g->lib, SDL_atoi(arg_value(app->argc, app->argv, "--lib-guest")), &guest);
        }
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
    const bool no_network = atest && (SDL_strcmp(atest, "offline") == 0 || SDL_strcmp(atest, "book") == 0 || g->perf.active || (ft_role && SDL_strcmp(ft_role, "offline") == 0));
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
        if (SDL_strcmp(fe_arg, "map") == 0 || SDL_strcmp(fe_arg, "books") == 0) { /* WORLD-1: the island */
            g->fe.state = FE_MAIN_MENU;
            open_map(g, app, fe_arg[0] == 'b' ? WM_BOOKS : WM_ARRIVED);
        }
    }
    if (g->perf.active && (SDL_strncmp(g->perf.scenario, "worldmap", 8) == 0 || SDL_strcmp(g->perf.scenario, "island") == 0)) {
        play_now = false; /* the map scene alone: the hospital is never built */
        g->fe.state = FE_MAIN_MENU;
        const char *sc = g->perf.scenario;
        open_map(g, app, SDL_strcmp(sc, "worldmap") == 0 ? WM_OVERVIEW : SDL_strcmp(sc, "worldmap_voyage") == 0 ? WM_BOOKS : WM_ARRIVED); /* travel starts parked, then drives on */
    }
    if (g->perf.active && (SDL_strcmp(g->perf.scenario, "menu") == 0 || SDL_strcmp(g->perf.scenario, "ui_juice") == 0 ||
                           SDL_strcmp(g->perf.scenario, "menu_to_map") == 0)) {
        play_now = false;
        g->fe.state = FE_MAIN_MENU;
        g->demo_lobby = true;
        if (SDL_strcmp(g->perf.scenario, "menu_to_map") == 0) { /* the move itself, looped (not the Reduse cross-dissolve) */
            g->move_demo = 4;
            g->force_full = true;
        }
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
    g->scene_override = -1;
    g->panel_lift = g->ride_lift = -1;
    g->level_switch_test = atest && SDL_strcmp(atest, "levelswitch") == 0 ? 120 : 0;
    if ((atest && (SDL_strcmp(atest, "institute") == 0 || SDL_strcmp(atest, "bookpeople") == 0 || SDL_strcmp(atest, "missions") == 0 || SDL_strcmp(atest, "book_ui") == 0 || SDL_strcmp(atest, "book") == 0)) || perf_run_scene(&g->perf)) { /* MAP-2: the institute test and perf scenario; BOOK-ENGINE: people, missions, book_ui */
        g->scene_override = (int)scene_kind_from_id(perf_run_scene(&g->perf) ? perf_run_scene(&g->perf) : "institute");
    }
    {
        const char *sc = arg_value(app->argc, app->argv, "--scene"); /* desktop: hospital | institute | devroom (also every level load) */
        if (sc) {
            g->scene_override = (int)scene_kind_from_id(SDL_strcmp(sc, "hospital") == 0 ? "hospital_v1" : sc);
            mg_log_info("scene: --scene %s", scene_kind_id((SceneKind)g->scene_override));
        }
    }
    if (play_now) {
        if (!ensure_world(g, app, g->scene_override >= 0 && !scene_kind_forced() ? (SceneKind)g->scene_override : scene_default_kind())) {
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
    {
        const char *pl = arg_value(app->argc, app->argv, "--log-npc-poses");
        g->npc_pose_log_tick = pl ? (uint32_t)SDL_atoi(pl) : 0u;
    }
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
    ui_fx_init(&g->ui_fx, &g->ui_anim, g->audio, &app->font_display);
    ui_attach(&g->ui, &g->ui_anim, &g->ui_fx, &app->font_display, &app->font_bold);
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

/* TILT-1, on the fixed tick: the strength, the sensor only while it is needed, the spring. */
static void tilt_step(Game *g, MgApp *app)
{
    const bool reduced = g->settings.ui_motion == 1 || app_reduce_motion() || g->uitest.reduced;
    g->tilt_s = tilt_strength(g->settings.ui_tilt, reduced, motion_low_power() || g->test_low_power);
    const bool want = g->tilt_s > 0.0f && !app->background;
    if (want != g->motion_on) { /* 60 Hz only while active and S > 0; activation snaps the neutral pose */
        g->motion_on = want;
        if (want) {
            motion_start();
        } else {
            motion_stop();
        }
        tilt_snap(&g->tilt); /* activation: offsets start at 0 */
    }
    float sx = 0.0f, sy = 0.0f, gx, gy, gz;
    const bool have = g->motion_on && motion_gravity(&gx, &gy, &gz);
    const bool flipped = SDL_GetCurrentDisplayOrientation(SDL_GetPrimaryDisplay()) == SDL_ORIENTATION_LANDSCAPE_FLIPPED;
    if (flipped != g->tilt_flipped) { /* the interface rotated: start from rest */
        g->tilt_flipped = flipped;
        tilt_snap(&g->tilt);
    }
    float rate = -1.0f;
    if (g->motion_on && !motion_rotation_rate(&rate)) {
        rate = -1.0f;
    }
    if (have) {
        tilt_screen_gravity(gx, gy, gz, g->tilt_flipped, &sx, &sy);
        g->tilt_raw[0] = gx, g->tilt_raw[1] = gy, g->tilt_raw[2] = gz, g->tilt_screen[0] = sx, g->tilt_screen[1] = sy;
    }
    g->tilt_have = have;
    /* desktop and tests: --debug tilt, or the mouse while T is held */
    g->tilt.forced = g->tilt_debug || app->input.key_down[SDL_SCANCODE_T];
    if (app->input.key_down[SDL_SCANCODE_T] && app->input.mouse_seen) {
        g->tilt.target_x = mg_clampf((app->input.mouse_x - app->width_pt * 0.5f) / (app->width_pt * 0.5f), -1.0f, 1.0f);
        g->tilt.target_y = mg_clampf((app->input.mouse_y - app->height_pt * 0.5f) / (app->height_pt * 0.5f), -1.0f, 1.0f);
    } else if (g->tilt_debug) {
        g->tilt.target_x = g->tilt_debug_x, g->tilt.target_y = g->tilt_debug_y;
    } else if (g->tilt_circle) { /* --tilt-sine (perf): a 1 Hz sine at full strength, no sensor */
        g->tilt.forced = true;
        g->tilt.target_x = sinf((float)app->time * 6.2831853f), g->tilt.target_y = 0.0f;
    }
    tilt_tick(&g->tilt, MG_TICK_DT, have, sx, sy, rate, app->time);
    if (g->tilt.shaken && g->tilt_s > 0.0f) { /* the frame plays the jiggle */
        g->tilt_shake_dir = g->tilt.shake_dir;
        g->tilt_shake_pending = true;
    }
}

/* --autotest levelswitch: hospital -> institute (stub) -> dev room -> hospital, three times, between ticks (nothing of
 * the scene is in a draw list). Every hospital build must leave the GPU pools and the level arena exactly where the
 * first one did, every unload must bring the pools back to the level mark, the world map's meshes (under the mark) stay. */
static void run_level_switch_check(Game *g, MgApp *app)
{
    static const SceneKind order[] = {SCENE_INSTITUTE, SCENE_DEVROOM, SCENE_HOSPITAL};
    bool ok = g->scene && scene_kind(g->scene) == SCENE_HOSPITAL && g->wm.loaded;
    char why[160] = "";
    uint32_t m0, vb0, ib0;
    gfx_mesh_usage(app->gfx, &m0, &vb0, &ib0);
    const size_t level0 = g->level.used, perm0 = app->perm.used;
    const int grids0 = g->nav ? nav_grid_count(g->nav) : 0;
    mg_log_info("levelswitch: hospital: %u meshes (%u over the mark), vb %u KB, ib %u KB, level arena %zu KB, perm %zu KB", m0, m0 - g->level_mesh_mark.meshes,
                vb0 >> 10, ib0 >> 10, level0 >> 10, perm0 >> 10);
    for (int round = 0; round < 3 && ok; round++) {
        for (int k = 0; k < 3 && ok; k++) {
            const double t0 = mg_time_now();
            if (!ensure_world(g, app, order[k])) {
                SDL_snprintf(why, sizeof(why), "could not build %s", scene_kind_id(order[k]));
                ok = false;
                break;
            }
            uint32_t m, vb, ib;
            gfx_mesh_usage(app->gfx, &m, &vb, &ib);
            mg_log_info("levelswitch: round %d: %s in %.1f ms: %u meshes, vb %u KB, ib %u KB, level arena %zu KB, lifts %d, npcs_ok %d, nav %d grids", round,
                        scene_name(g->scene), (mg_time_now() - t0) * 1000.0, m, vb >> 10, ib >> 10, g->level.used >> 10, lift_count(g), g->npcs_ok,
                        g->nav ? nav_grid_count(g->nav) : 0);
            if (scene_kind(g->scene) != order[k]) {
                SDL_snprintf(why, sizeof(why), "built %s, wanted %s", scene_kind_id(scene_kind(g->scene)), scene_kind_id(order[k]));
                ok = false;
            } else if (order[k] == SCENE_INSTITUTE && g->npcs_ok && !g->bp_ok) {
                SDL_snprintf(why, sizeof(why), "the institute has the hospital's ambient roster");
                ok = false;
            } else if (order[k] == SCENE_DEVROOM && (g->nav || lift_count(g) != 0)) {
                SDL_snprintf(why, sizeof(why), "the dev room has a nav or lifts");
                ok = false;
            } else if (order[k] == SCENE_HOSPITAL && (m != m0 || vb != vb0 || ib != ib0 || g->level.used != level0 || app->perm.used != perm0 || !g->npcs_ok ||
                                                     !g->nav || nav_grid_count(g->nav) != grids0 || lift_count(g) != 1)) {
                SDL_snprintf(why, sizeof(why), "hospital rebuilt differently: %u/%u meshes, vb %u/%u, level %zu/%zu, perm %zu/%zu", m, m0, vb, vb0, g->level.used, level0,
                             app->perm.used, perm0);
                ok = false;
            } else if (m < g->level_mesh_mark.meshes || !g->wm.loaded) {
                SDL_snprintf(why, sizeof(why), "the pools went under the level mark");
                ok = false;
            }
        }
    }
    if (ok) { /* back in the hospital: play on from the entrance for a moment */
        player_init(&g->player, scene_spawn(g->scene, 0), scene_spawn_yaw(g->scene));
        g->cam.initialized = g->td.initialized = false;
        g->fade_snap = true;
    }
    mg_log_info("levelswitch: %s%s%s", ok ? "all builds match" : "FAILED: ", ok ? "" : why, "");
    g->level_switch_test = ok ? -1 : 0;
    g->level_switch_end = ok ? app->time + 3.0 : app->time;
    if (!ok) {
        mg_log_info("AUTOTEST FAILED");
        app->quit_requested = true;
    }
}

void game_tick(MgApp *app)
{
    Game *g = (Game *)app->game;
    tilt_step(g, app);
    if (g->level_switch_test > 0 && --g->level_switch_test == 0) {
        run_level_switch_check(g, app);
    } else if (g->level_switch_test < 0 && app->time >= g->level_switch_end) { /* it drew and played for 3 s */
        mg_log_info("AUTOTEST PASSED");
        app->quit_requested = true;
        g->level_switch_test = 0;
    }
    if (!g->playing) {
        if (g->fe.state == FE_MAP) {
            worldmap_tick(&g->wm, MG_TICK_DT);
        }
        if (g->menu_world_on && g->npcs_on && !g->settings.freeze_npcs) { /* the people outside the menu */
            npc_step(g, app);
        }
        return;
    }
    scene_tick(g->scene, &g->world, MG_TICK_DT);
    lifts_call(g, g->player.kin.pos);
    if (g->riding) { /* still moving? */
        const Elevator *re = lift_get(g, g->ride_lift);
        g->riding = re && elevator_moving(re);
    }
    for (int i = 0; i < lift_count(g) && !g->riding; i++) { /* a car leaves: is the player in it? */
        const Elevator *e = lift_get(g, i);
        const HMM_Vec3 p = g->player.kin.pos;
        if (elevator_moving(e) && lift_holds_xz(e, p) && fabsf(p.Y - (float)e->from_floor * e->storey) < 0.5f) {
            g->riding = true;
            g->ride_lift = i;
            g->ride_offset = p.Y - (float)e->from_floor * e->storey;
        }
    }
    for (int i = 0; i < lift_count(g); i++) {
        Elevator *e = lift_get(g, i);
        if (e->arrived) {
            e->arrived = false;
            audio_ding(g->audio);
            if (g->playing && lift_holds_xz(e, g->player.kin.pos)) {
                ui_haptic(HAPTIC_LIFT_ARRIVE); /* only for a rider: not a car the player cannot see */
            }
            g->stats.rides++;
            stats_reset_position(&g->stats);
        }
    }
    if (g->riding) { /* travel with the car: no physics through the slabs */
        g->player.prev_pos = g->player.kin.pos;
        g->player.kin.pos.Y = lift_get(g, g->ride_lift)->car_y + g->ride_offset;
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
        save_mark_visited(&g->save, scene_floor_of(g->scene, p), col + (p.Z > -9.4f ? 8 : 0)); /* storeys 0..7 */
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

/* The roster from `seed`, or in the institute the pack's people (BOOK-ENGINE) */
static void npcs_start(Game *g, uint32_t seed)
{
    if (g->bp_ok) {
        book_people_start(&g->bp, seed);
    } else {
        npc_world_start(&g->npcs, seed);
    }
}
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
static ui_frame_t hud_frame(const MgApp *app) /* phones: the safe area again; iPad: edge to edge (ui_hud_frame) */
{
    return ui_hud_frame(app->width_pt, app->height_pt, app->safe_left, app->safe_right, app->safe_top, app->safe_bottom, 0.0f); /* corner 0: the pause button matches settings_menu's */
}

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
    SDL_snprintf(s->scene_id, sizeof(s->scene_id), "%s", scene_kind_id(scene_kind(g->scene))); /* v7: whose coordinates pos is in */
    s->play_seconds = g->save_play_base + (uint32_t)g->stats.play_seconds;
    s->elevator_rides = g->save_rides_base + (uint32_t)g->stats.rides;
    g->has_save = save_write(s, g->profile_name) || g->has_save;
}

/* ------------------------------------------------------------ WORLD-1/2 */

static const WorldBookDef *cur_book(const Game *g) { return &g->books.books[g->play_book]; }

/* The island played last (Maps opens it), else the first. */
static int last_book_index(const Game *g)
{
    const int i = world_book_find(&g->books, g->prefs.world.last_book);
    return i < 0 ? 0 : i;
}

static void maps_tag_update(Game *g)
{
    const WorldBookDef *b = &g->books.books[last_book_index(g)];
    const IslandCompletion ic = world_island_completion(&g->prefs.world, b);
    if (ic.known) {
        SDL_snprintf(g->maps_tag, sizeof(g->maps_tag), "%s %d%%", b->name, ic.percent);
    } else {
        SDL_snprintf(g->maps_tag, sizeof(g->maps_tag), "%s %s", b->name, str("wm_unknown"));
    }
}

/* book_state: send our newer choice once the account is there */
static void push_book_state(Game *g)
{
    if (!g->net || !g->profile_loaded) {
        return;
    }
    for (int i = 0; i < g->prefs.world.book_count; i++) {
        WorldBookState *s = &g->prefs.world.books[i];
        if (s->unsynced && s->last_level[0]) {
            net_save_book_state(g->net, s->book, s->last_level, s->updated_ms);
            s->unsynced = false;
        }
    }
}

/* ride_state: the account keeps the shuffle bag, so a reinstall does not reset the no-repeat rule */
static void push_rides(Game *g)
{
    WorldRides *r = &g->prefs.world.rides;
    if (!r->unsynced || !g->net || !g->profile_loaded) {
        return;
    }
    char json[256];
    if (world_rides_json(r, json, sizeof(json)) < (int)sizeof(json)) {
        net_save_ride_state(g->net, json, r->updated_ms);
        r->unsynced = false;
    }
}

static int level_index(const Game *g, const char *id)
{
    const int i = world_level_find(cur_book(g), id);
    return i < 0 ? 0 : i;
}

/* The islands: BOOKS after New Game, ARRIVED on the last played hospital of the last played island
 * from Maps and the pause card. */
/* Finish the menu's preload of the islands (or the whole load) now: the map, and before the first level scene. */
static void finish_map_preload(Game *g, MgApp *app)
{
    if (!g->wm.loaded && !g->wm_tried) {
        prof_allocs_exempt(true); /* once: the islands' meshes */
        int r;
        while ((r = worldmap_load_step(&g->wm, &g->books, app->gfx, &app->perm)) == 0) {
        }
        g->wm_tried = r < 0;
        prof_allocs_exempt(false);
        arena_release_unused(&app->perm);
    }
}

static bool open_map(Game *g, MgApp *app, WmState state)
{
    const double t0 = (double)SDL_GetTicksNS() * 1e-6;
    finish_map_preload(g, app);
    if (!g->wm.loaded) {
        frontend_toast(&g->fe, "Could not load the map", app->time);
        return false;
    }
    g->wm.rides = &g->prefs.world.rides;
    const int island = last_book_index(g);
    worldmap_open(&g->wm, state, island, &g->prefs.world);
    maps_tag_update(g);
    g->fe.depth = 0;
    g->fe.state = FE_MAIN_MENU;
    frontend_go(&g->fe, FE_MAP);
    mg_log_info("world: map open (%s) in %.1f ms, %s / %s", state == WM_BOOKS ? "books" : state == WM_OVERVIEW ? "overview" : "arrived", (double)SDL_GetTicksNS() * 1e-6 - t0,
                g->books.books[island].id, g->books.books[island].levels[g->wm.isles[island].selected].id);
    return true;
}

static void start_loading(Game *g, int what);

/* Join on the card: the level's scene through the normal New Game / lobby path. */
static void join_level(Game *g, MgApp *app, int level)
{
    g->play_book = g->wm.cur;
    const WorldBookDef *b = cur_book(g);
    const WorldLevelDef *lv = &b->levels[level];
    g->play_level = level;
    world_set_last_level(&g->prefs.world, b->id, lv->id, worldmap_stop_s(wm_cur(&g->wm), level), wall_clock_ms());
    SDL_snprintf(g->prefs.world.last_book, sizeof(g->prefs.world.last_book), "%s", b->id);
    push_book_state(g);
    save_prefs(g);
    SaveGame cur;
    const bool same = g->has_save && save_read(&cur, g->profile_name) && SDL_strcmp(cur.book_id, b->id) == 0 && SDL_strcmp(cur.level_id, lv->id) == 0;
    mg_log_info("world: join %s/%s (scene %s%s)", b->id, lv->id, lv->scene, same ? ", where the save left off" : "");
    (void)app;
    start_loading(g, same ? 3 : 5);
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
    maps_tag_update(g);
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
    case MENU_ERASE_PROGRESS:
        erase_progress(g);
        frontend_toast(&g->fe, str("progress_erased"), app->time);
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
    case MENU_JOURNAL: /* BOOK-ENGINE S4: the page replaces the card; its tab = the tracked case's chapter */
        g->mui.journal = g->missions.on;
        if (g->missions.on && g->missions.tracked >= 0) {
            g->mui.arc_tab = SDL_max(missions_arc_of_chapter(&g->book, g->book.missions[g->missions.tracked].chapter), 0);
        }
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
        int f = SDL_clamp(g->settings.teleport_floor, 0, scene_floor_count(g->scene) - 1);
        player_init(&g->player, scene_floor_lobby(g->scene, f), 0.0f);
        lifts_place(g, f);
        g->cam.initialized = g->td.initialized = false;
        g->fade_snap = true;
        break;
    }
    case MENU_MAIN_MENU:
        leave_play(g, app);
        break;
    case MENU_OPEN_MAP:
        leave_play(g, app);
        g->move_from_pause = true; /* LIB-1: from a hospital the map keeps its scene switch */
        open_map(g, app, WM_ARRIVED);
        break;
    case MENU_DEBUG_PROGRESS: {
        const char *id = cur_book(g)->levels[g->play_level].id;
        const int pct = world_completion_percent(world_level_completion(&g->prefs.world, cur_book(g)->id, id));
        world_debug_progress(&g->prefs.world, id, pct >= 100 ? 0 : pct + 25);
        mg_log_info("world: test progress %s = %d%%", id, pct >= 100 ? 0 : pct + 25);
        break;
    }
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
    ui_shape(app->gfx, &(UiShape){.x = cx - mw * 0.5f - 14.0f, .y = y + k_ui.lip_pill, .w = mw + 28.0f, .h = 28.0f, .radius = -1.0f, .color = ui_rgba(k_ui.bad_lip)});
    ui_shape(app->gfx, &(UiShape){.x = cx - mw * 0.5f - 14.0f, .y = y, .w = mw + 28.0f, .h = 28.0f, .radius = -1.0f, .color = ui_rgba(k_ui.bad)});
    text_draw_centered(app->gfx, &app->font_bold, cx, y + 14.0f, 14.0f, ui_rgba(k_ui.white), msg);
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
                         g->session.account_error[0] ? g->session.account_error : g->account_msg,
                         !g->playing && !g->play_paused};
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
        ui_haptic(HAPTIC_LINKED);
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
    progress_open(g, g->profile.user_id); /* BOOK-ENGINE: this account's file, then the server's copy merged in */
    progress_commit(g, true);
    for (int i = 0; i < g->profile.book_count; i++) { /* book_state: the newer choice wins */
        const NetBookState *b = &g->profile.books[i];
        world_progress_merge_remote(&g->prefs.world, b->book, b->level, b->updated_ms);
    }
    push_book_state(g);
    if (g->profile.ride_state[0] && g->profile.ride_updated_ms > g->prefs.world.rides.updated_ms) { /* the account's bag is newer (a reinstall) */
        world_rides_from_json(&g->prefs.world.rides, g->profile.ride_state);
        g->prefs.world.rides.updated_ms = g->profile.ride_updated_ms;
        g->prefs.world.rides.unsynced = false;
        mg_log_info("world: ride bag from the account (%d left, last %s)", g->prefs.world.rides.bag_left, world_ride_name((WorldRide)g->prefs.world.rides.last));
    } else if (g->prefs.world.rides.updated_ms > g->profile.ride_updated_ms && g->prefs.world.rides.updated_ms > 0.0) {
        g->prefs.world.rides.unsynced = true; /* ours is newer */
        push_rides(g);
    }
    g->prefs.has_profile_cache = true;
    SDL_snprintf(g->prefs.cache_name, sizeof(g->prefs.cache_name), "%s", g->profile.display_name);
    SDL_snprintf(g->prefs.cache_user_id, sizeof(g->prefs.cache_user_id), "%s", g->profile.user_id);
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
        ui_haptic(HAPTIC_INVITE_DECLINED);
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
            if (g->menu_world_on) { /* §8: the panel at once, as before; a paper plane flies in as its picture */
                menu_library_receive_plane(&g->lib, r->id);
            }
        }
    }
    if (l->grant_ready) {
        ui_haptic(HAPTIC_INVITE_ACCEPTED); /* our join was accepted */
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
    gfx_ui_depth(app->gfx, k_ui.tilt_depth[3], 0); /* TILT-1: a notice is a near layer */
    const NoticeResult res = notice_panel_draw(&g->notice, &g->ui, &g->heads, &f, app->time, mg_minf(app->frame_dt, 0.1f), str("friend_accept"),
                                               str("friend_decline"), &kind, id);
    if (g->notice.just_shown) {
        if (g->notice.q[0].kind == NOTICE_JOIN) {
            lobby_request_shown(&g->lobby, g->notice.q[0].id);
        } else if (g->notice.q[0].kind == NOTICE_INVITE) {
            lobby_invite_shown(&g->lobby, g->notice.q[0].id);
        }
        if (g->settings.ui_sounds) {
            audio_cue(g->audio, AUDIO_CUE_TICK); /* two notes */
        }
    }
    if (res != NOTICE_NONE && kind == NOTICE_JOIN) {
        lobby_respond(&g->lobby, id, res == NOTICE_ACCEPT);
    } else if (res != NOTICE_NONE && kind == NOTICE_INVITE) {
        lobby_invite_answer(&g->lobby, id, res == NOTICE_ACCEPT);
        menu_library_plane_close(&g->lib); /* §8: the landed plane shrinks away with its panel */
    } else if (res != NOTICE_NONE && kind == NOTICE_FRIEND) {
        friends_respond(&g->friends, id, res == NOTICE_ACCEPT);
    }
}

/* LOADING: build the hospital over a few frames, then start playing. */
static void load_step(Game *g, MgApp *app)
{
    switch (g->load_step++) {
    case 0: { /* one frame to show the bar; which level (and so which scene) this is */
        SaveGame peek;
        if (g->pending_play == 3 && save_read(&peek, g->profile_name)) { /* Continue: the save's level */
            g->play_book = SDL_max(world_book_find(&g->books, peek.book_id), 0);
            g->play_level = level_index(g, peek.level_id);
        } else if (g->pending_play == 4) { /* a friend's game: the host's level (map_id = book * 64 + level) */
            g->play_book = SDL_clamp(g->lobby.grant.map_id / 64, 0, g->books.count - 1);
            g->play_level = SDL_clamp(g->lobby.grant.map_id % 64, 0, cur_book(g)->level_count - 1);
        }
        break;
    }
    case 1: {
        const SceneKind kind = level_scene_kind(g, g->play_book, g->play_level);
        mg_log_info("level: %s/%s -> scene %s", cur_book(g)->id, cur_book(g)->levels[g->play_level].id, scene_kind_id(kind));
        if (!ensure_world(g, app, kind)) {
            frontend_toast(&g->fe, "Could not load the hospital", app->time);
            g->fe.state = FE_MAIN_MENU;
        }
        gfx_settle_uploads(app->gfx); /* the hospital's meshes are in */
        break;
    }
    default:
        player_init(&g->player, scene_spawn(g->scene, 0), scene_spawn_yaw(g->scene));
        g->offline_play = g->pending_play == 0 || g->pending_play == 3 || g->pending_play == 5;
        g->keep_save_pos = false;
        net_set_quiet(g->net, g->offline_play && !g->friends.signed_in); /* online, a solo game is a lobby */
        g->npc_epoch_ms = g->pending_play == 4 ? g->lobby.grant.start_tick : 0.0;
        g->save_play_base = g->save_rides_base = 0;
        if (g->pending_play == 3 && save_read(&g->save, g->profile_name)) { /* Continue */
            g->npc_resume_tick = g->save.npc_tick;
            /* v7: the save says whose coordinates it holds; a level whose scene changed since (a v6 gastro save, hospital
             * coordinates, now the institute) starts at the new scene's entrance */
            const bool same_scene = SDL_strcmp(g->save.scene_id, scene_kind_id(scene_kind(g->scene))) == 0;
            if (same_scene) {
                player_init(&g->player, g->save.pos, g->save.yaw);
#ifndef MG_CAMERA_THIRDPERSON
                /* v2 saves store the camera yaw; the camera is fixed now */
#endif
                if (g->save.floor > 0) {
                    lifts_place(g, (int)g->save.floor);
                }
            } else {
                mg_log_info("save: v%u in scene %s, the level is %s now: Continue starts at its entrance", g->save.version, g->save.scene_id,
                            scene_kind_id(scene_kind(g->scene)));
                g->npc_resume_tick = 0;
            }
            g->save_play_base = g->save.play_seconds - (uint32_t)g->stats.play_seconds;
            g->save_rides_base = g->save.elevator_rides - (uint32_t)g->stats.rides;
            g->play_book = SDL_max(world_book_find(&g->books, g->save.book_id), 0);
            g->play_level = level_index(g, g->save.level_id);
            SDL_snprintf(g->prefs.world.last_book, sizeof(g->prefs.world.last_book), "%s", cur_book(g)->id);
            g->keep_save_pos = same_scene;
            world_set_last_level(&g->prefs.world, cur_book(g)->id, cur_book(g)->levels[g->play_level].id, -1.0f, wall_clock_ms());
            push_book_state(g);
        } else if (g->pending_play == 5) { /* Join: the level from its entrance; play time and rides carry on */
            SaveGame prev;
            const bool had = save_read(&prev, g->profile_name);
            SDL_zero(g->save);
            g->npc_resume_tick = 0;
            g->save_play_base = (had ? prev.play_seconds : 0u) - (uint32_t)g->stats.play_seconds;
            g->save_rides_base = (had ? prev.elevator_rides : 0u) - (uint32_t)g->stats.rides;
        } else if (g->pending_play == 0) { /* New Game: a clean slot */
            save_delete(g->profile_name);
            g->has_save = false;
            SDL_zero(g->save);
            g->npc_resume_tick = 0;
            g->save_play_base = (uint32_t)0 - (uint32_t)g->stats.play_seconds;
            g->save_rides_base = (uint32_t)0 - (uint32_t)g->stats.rides;
        }
        if (g->pending_play == 4) { /* a friend's game: the host's level */
            g->play_book = SDL_clamp(g->lobby.grant.map_id / 64, 0, g->books.count - 1); /* map_id = book * 64 + level */
            g->play_level = SDL_clamp(g->lobby.grant.map_id % 64, 0, cur_book(g)->level_count - 1);
        }
        SDL_snprintf(g->save.book_id, sizeof(g->save.book_id), "%s", cur_book(g)->id);
        SDL_snprintf(g->save.level_id, sizeof(g->save.level_id), "%s", cur_book(g)->levels[g->play_level].id);
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
            lobby_start_host(&g->lobby, g->play_book * 64 + g->play_level, npc_seed_for(g, app)); /* map_id = book * 64 + level; online when signed in, offline nothing leaves the device */
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

/* LIB-1: the library prototype's light: sea-blue clear colour, ambient 0.5 + a 0.5 key from (-0.55, 1, 0.5), the
 * two-band toon ramp, no fog (three.js r128 AmbientLight / DirectionalLight / MeshToonMaterial in the prototype). */
#define LIB_SEA 0x3FB6D8u
static void library_lighting(GfxSceneParams *sp, float time)
{
    sp->sun_dir = HMM_NormV3(HMM_V3(-0.55f, 1.0f, 0.5f));
    sp->sun_color = HMM_V3(0.5f, 0.5f, 0.5f);
    sp->ambient = HMM_V3(0.5f, 0.5f, 0.5f);
    sp->sky_horizon = sp->sky_zenith = mg_rgb(LIB_SEA);
    sp->fog_density = 0.0f;
    sp->sun_disc.W = sp->moon_disc.W = 0.0f;
    sp->stars = 0.0f;
    sp->two_band = 1.0f;
    sp->time = time;
}

/* out = a .. b by k: the light sets mixed (each surface lit by both), sky, fog, discs and stars blended: no step anywhere */
static void lighting_mix(GfxSceneParams *out, const GfxSceneParams *a, const GfxSceneParams *b, float k)
{
    /* the sun is not turned (a toon band would flip somewhere as it turns): both sets are lit and the results mixed */
    const HMM_Vec3 sun2_dir = b->sun_dir, sun2_color = b->sun_color, ambient2 = b->ambient;
    const float two_band2 = b->two_band;
    out->sun_dir = a->sun_dir, out->sun_color = a->sun_color, out->ambient = a->ambient, out->two_band = a->two_band;
    out->sun2_dir = sun2_dir, out->sun2_color = sun2_color, out->ambient2 = ambient2, out->two_band2 = two_band2;
    out->light_mix = k;
    out->sky_horizon = HMM_LerpV4(a->sky_horizon, k, b->sky_horizon);
    out->sky_zenith = HMM_LerpV4(a->sky_zenith, k, b->sky_zenith);
    out->fog_color = HMM_LerpV3(a->fog_color, k, b->fog_color);
    out->fog_density = mg_lerpf(a->fog_density, b->fog_density, k);
    out->sun_disc.W = mg_lerpf(a->sun_disc.W, b->sun_disc.W, k);
    out->moon_disc.W = mg_lerpf(a->moon_disc.W, b->moon_disc.W, k);
    out->stars = mg_lerpf(a->stars, b->stars, k);
}

static bool motion_reduced(const Game *g);

/* LIB-1 §8: an invite or a friend request sent (the RPC went out at once) is a paper plane thrown from my seat; one sent from the
 * Friends page flies once I am back on the menu (within 30 s). Nothing waits for it. */
static void library_planes(Game *g, MgApp *app, bool on_menu)
{
    if (g->lobby.invites_sent != g->plane_inv_seen) {
        g->plane_inv_seen = g->lobby.invites_sent;
        SDL_snprintf(g->plane_to, sizeof(g->plane_to), "%s", g->lobby.last_invite);
        g->plane_at = app->time;
    }
    if (g->friends.requests_sent != g->plane_req_seen) {
        g->plane_req_seen = g->friends.requests_sent;
        SDL_snprintf(g->plane_to, sizeof(g->plane_to), "%s", g->friends.last_request);
        g->plane_at = app->time;
    }
    if (g->plane_at > 0.0) {
        if (app->time - g->plane_at > 30.0) {
            g->plane_at = 0.0;
        } else if (on_menu && menu_library_throw_plane(&g->lib, g->plane_to)) {
            g->plane_at = 0.0;
        }
    }
}

/* The main menu's backdrop (LIB-1): the library reading room, the hero studying in seat 0. Also behind
 * onboarding, Settings and Friends. The hospital is no longer loaded for the menu: a game builds it while
 * loading. */
static bool menu_world(Game *g, MgApp *app)
{
    const int st = g->fe.state;
    const bool onboarding = (st == FE_WELCOME || st == FE_USERNAME || st == FE_CREATOR) && !g->play_paused;
    if ((st != FE_MAIN_MENU && st != FE_SETTINGS && st != FE_FRIENDS && !onboarding) || !g->lib.ok) {
        if (g->lib.ok) {
            menu_library_travel_finish(&g->lib); /* §8: the game loading, the map: every move ends now */
        }
        g->menu_world_on = false;
        return false;
    }
    const bool opened = !g->menu_world_on; /* members already in the lobby are seated on this frame */
    g->menu_world_on = true;
    { /* §5: my table is dressed from my lobby's seed: my user id (cached for the first frame), the name before any sign-in */
        const char *id = g->profile.user_id[0] ? g->profile.user_id : g->prefs.cache_user_id[0] ? g->prefs.cache_user_id
                         : g->profile.display_name[0] ? g->profile.display_name : g->prefs.cache_name;
        menu_library_set_lobby_seed(&g->lib, lib_seed_of(id));
    }
    { /* §8: the lobby's table (the host's), trips, arrivals and leavers; a full-screen page or Reduse: at once */
        const bool page = st == FE_SETTINGS || st == FE_FRIENDS || st == FE_CREATOR; /* the Locker too */
        if (page) {
            menu_library_travel_finish(&g->lib);
        }
        menu_library_lobby(&g->lib, g->menu_rows, g->lobby_visible ? g->menu_row_count : 0, opened, page || motion_reduced(g));
        if (g->lib.dissolve_armed && !page) { /* Reduse: this frame is kept and dissolved over the trip done at once (next frame) */
            gfx_dissolve_capture(app->gfx);
            g->dissolve_t0 = app->time;
        }
    }
    library_planes(g, app, st == FE_MAIN_MENU);
    if (st == FE_WELCOME && !g->lib_hero_walked) { /* a fresh install: the hero comes in once */
        g->lib_hero_walked = true;
        menu_library_hero_walk_in(&g->lib);
    }
    const bool panel = st == FE_MAIN_MENU && g->lobby_visible;
    menu_library_set_creator(&g->lib, st == FE_CREATOR, frontend_menu_right(app, FE_CREATOR));
    const float free_l = frontend_menu_right(app, st == FE_CREATOR ? FE_MAIN_MENU : (FeState)st) + 8.0f, free_r = frontend_menu_free_right(app, panel) - 8.0f;
    GfxSceneParams sp = {0};
    menu_library_camera(&g->lib, app, free_l, free_r, 12.0f, app->height_pt - 12.0f, &sp);
    { /* TILT-1: the backdrop sways around its framing point, opposite to the UI's shift (the gameplay camera never does) */
        const HMM_Vec3 look = g->lib.target, d = HMM_SubV3(sp.eye, look);
        const float yaw = HMM_AngleDeg(k_ui.tilt_cam_yaw_deg * g->tilt.tx * g->tilt_s), pitch = HMM_AngleDeg(k_ui.tilt_cam_pitch_deg * g->tilt.ty * g->tilt_s);
        const HMM_Vec3 right = HMM_NormV3(HMM_Cross(d, HMM_V3(0, 1, 0)));
        const HMM_Mat4 r = HMM_MulM4(HMM_Rotate_RH(yaw, HMM_V3(0, 1, 0)), HMM_Rotate_RH(pitch, right));
        sp.eye = HMM_AddV3(look, HMM_MulM4V4(r, HMM_V4V(d, 0.0f)).XYZ);
        sp.view = HMM_LookAt_RH(sp.eye, look, HMM_V3(0, 1, 0));
    }
    g->view_proj = HMM_MulM4(sp.proj, sp.view);
    g->last_eye = sp.eye;
    library_lighting(&sp, (float)app->time);
    gfx_set_scene(app->gfx, &sp);
    for (int i = 0; i < LOBBY_MAX_PLAYERS; i++) {
        g->menu_slots[i] = menu_library_seat_world(&g->lib, i < LIB_SEATS ? i : 0);
    }
    g->menu_hero = g->menu_slots[0];
    const AvatarLook hero = avatar_look_from_skin(g->profile.skin);
    g->lib.view_eye = g->last_eye;
    g->lib.view_proj = g->view_proj, g->lib.has_vp = true;
    menu_library_draw(&g->lib, app->gfx, &g->avatar_meshes, &hero, g->lib_still ? 0.0f : mg_minf(app->frame_dt, 0.1f));
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
    { /* LIB-1: the table and its four seats between the menu column and the lobby panel, on screen */
        float box[4];
        menu_library_fit_box(&g->lib, &g->view_proj, w, app->height_pt, box);
        mg_log_info("autotest lobbylayout: table and seats %.0f .. %.0f x %.0f .. %.0f pt, free %.0f .. %.0f", (double)box[0], (double)box[1],
                    (double)box[2], (double)box[3], (double)col_r, (double)px);
        if (!bad && (box[0] < col_r || box[1] > px || box[2] < 0.0f || box[3] > app->height_pt)) {
            bad = "the table and its seats overlap the menu column or the lobby panel, or leave the screen";
        }
    }
    mg_log_info("autotest lobbylayout: %.0f x %.0f, panel %.0f,%.0f %.0f x %.0f, %d rows", (double)w, (double)app->height_pt, (double)px, (double)py, (double)pw, (double)ph, ctx->row_count);
    if (bad) {
        mg_log_error("autotest lobbylayout: %s", bad);
    }
    mg_log_info("AUTOTEST %s", bad ? "FAILED" : "PASSED");
    app->quit_requested = true;
}

/* Settings -> Erase progress (what New Game was): a clean slot, every island's ambulance on its start pad,
 * no last hospital, the first island current. */
static void erase_progress(Game *g)
{
    save_delete(g->profile_name);
    g->has_save = false;
    SDL_zero(g->save);
    for (int i = 0; i < g->books.count; i++) {
        world_progress_reset(&g->prefs.world, g->books.books[i].id, wall_clock_ms());
    }
    SDL_snprintf(g->prefs.world.last_book, sizeof(g->prefs.world.last_book), "%s", g->books.books[0].id);
    if (g->progress_uid[0]) { /* BOOK-ENGINE: the book's facts and cases too, here and on the account */
        book_progress_reset(&g->progress, wall_clock_ms());
        if (g->net && SDL_strcmp(g->progress_uid, "local") != 0) {
            net_reset_book_progress(g->net, PROGRESS_BOOK);
        }
        progress_live(g);
    }
    save_prefs(g);
    maps_tag_update(g);
    mg_log_info("settings: progress erased");
}

/* the building stands on its pad (the pad is drawn 0.03 above the ground; the prototype lifts the library 0.07) */
#define LIB_ABOVE_PAD 0.07f

/* LIB-1 step 6: the map preloads behind the menu, one island per frame; the library stands on the island
 * played last. */
static void map_preload(Game *g, MgApp *app)
{
    if (!g->wm.loaded && !g->wm_tried) {
        prof_allocs_exempt(true); /* once: the islands' meshes */
        frame_note("map preload step");
        const int r = worldmap_load_step(&g->wm, &g->books, app->gfx, &app->perm);
        if (r == 1) {
            gfx_settle_uploads(app->gfx); /* the islands are in: their staging buffers go now, not whenever Metal reclaims them */
        }
        prof_allocs_exempt(false);
        g->wm_tried = r < 0;
        if (r != 0) {
            arena_release_unused(&app->perm);
        }
    }
    if (g->wm.loaded && g->move_state == 0) {
        const WmIsland *is = &g->wm.isles[last_book_index(g)];
        const WorldIsland *w = &is->island;
        if (w->has_library) {
            menu_library_place(&g->lib, HMM_V3(is->off.X + w->library.X, w->pad_y + LIB_ABOVE_PAD, is->off.Z + w->library.Y), w->library_yaw_deg);
            g->lib_placed = true;
        }
    }
}

#define MOVE_ISLAND_SIZE 400.0f /* the far plane's margin */
#define MOVE_MAP_UI_AT 0.9f

/* Maps / New Game from the menu: the camera leaves the table and lands on the map's camera, no cut. */
#define DISSOLVE_S 0.3f

static bool motion_reduced(const Game *g) { return !g->force_full && (g->force_reduced || g->settings.ui_motion == 1 || g->sys_reduce_motion); }

static bool begin_map_move(Game *g, MgApp *app, WmState state)
{
    if (!g->wm.loaded || !g->lib_placed) {
        return open_map(g, app, state);
    }
    if (motion_reduced(g)) { /* Animații interfață = Reduse: a 300 ms cross-dissolve instead of the move */
        gfx_dissolve_capture(app->gfx);
        g->dissolve_t0 = app->time;
        menu_library_travel_finish(&g->lib);
        g->lib.shell_k = 1.0f;
        return open_map(g, app, state);
    }
    g->wm.rides = &g->prefs.world.rides;
    worldmap_open(&g->wm, state, last_book_index(g), &g->prefs.world);
    maps_tag_update(g);
    LibCamPose map;
    worldmap_view_goal(&g->wm, app, &map.target, &map.dist, &map.pitch_deg, &map.fov_deg);
    map.yaw_deg = 0.0f;
    const LibScene *sc = &g->lib.sc;
    lib_move_begin(&g->move, menu_library_cam_pose(&g->lib), map, sc->transition.duration_s, sc->transition.hump, sc->transition.shell_fade[0],
                   sc->transition.shell_fade[1], true);
    menu_library_travel_finish(&g->lib); /* §8: Maps finishes a trip (from the camera where it is: no jump) */
    g->move_state = 1;
    g->move_t0 = app->time;
    g->move_entered = false;
    mg_log_info("library: move to the map (%s) over %.1f s", state == WM_BOOKS ? "books" : "arrived", (double)sc->transition.duration_s);
    return true;
}

/* Back on the map: the same path, reversed, onto the studying hero. */
static void begin_back_move(Game *g, MgApp *app)
{
    const WmCam c = g->wm.cam;
    LibCamPose map = {c.target, c.dist, c.pitch_deg, 0.0f, wm_cur(&g->wm)->island.fov_deg};
    g->move_state = 0; /* places the library on the island played last */
    map_preload(g, app);
    const FeState saved = g->fe.state;
    g->fe.state = FE_MAIN_MENU; /* the menu camera for the menu's layout */
    GfxSceneParams sp = {0};
    menu_library_set_creator(&g->lib, false, 0.0f);
    g->lib.creator_t = 0.0f;
    const bool panel = g->lobby_visible;
    menu_library_camera(&g->lib, app, frontend_menu_right(app, FE_MAIN_MENU) + 8.0f, frontend_menu_free_right(app, panel) - 8.0f, 12.0f,
                        app->height_pt - 12.0f, &sp);
    g->fe.state = saved;
    const LibScene *sc = &g->lib.sc;
    lib_move_begin(&g->move, menu_library_cam_pose(&g->lib), map, sc->transition.duration_s, sc->transition.hump, sc->transition.shell_fade[0],
                   sc->transition.shell_fade[1], false);
    g->move_state = 2;
    g->move_t0 = app->time;
    prof_allocs_exempt(true); /* one prefs.json write per Back (cJSON): a user action's I/O, like the map's one load */
    save_prefs(g);
    prof_allocs_exempt(false);
    mg_log_info("library: move back to the menu over %.1f s", (double)sc->transition.duration_s);
}

/* One frame of the move: the map's world and the library, the shell fading in / out, no input. */
static void move_frame(Game *g, MgApp *app)
{
    const float dt = mg_minf(app->frame_dt, 0.1f);
    const bool done = lib_move_step(&g->move, dt);
    const float e = done ? (g->move_state == 1 ? 1.0f : 0.0f) : lib_move_e(&g->move);
    const LibCamPose c = lib_move_pose(&g->move, e);
    HMM_Mat4 view, proj;
    HMM_Vec3 eye;
    lib_cam_matrices(&c, app->width_pt / app->height_pt, MOVE_ISLAND_SIZE, &view, &proj, &eye);
    g->lib.shell_k = lib_move_shell(&g->move, e);
    g->wm.cam_override = true;
    g->wm.ov_view = view, g->wm.ov_proj = proj, g->wm.ov_eye = eye;
    g->ui.input_off = true;
    if (g->move_state == 1 && g->move.t >= MOVE_MAP_UI_AT && !g->move_entered) { /* the map's UI enters for the last tenth */
        g->move_entered = true;
        g->fe.depth = 0;
        g->fe.state = FE_MAIN_MENU;
        frontend_go(&g->fe, FE_MAP);
    }
    g->wm.hud_off = !(g->move_state == 1 && g->move_entered);
    GfxSceneParams sp = {0};
    scene_defaults(&sp, (float)app->time, &g->day_state);
    (void)worldmap_frame(&g->wm, &g->ui, app, &sp, g->frame_alpha);
    { /* the library's light becomes the map's along the move (and back): e 0 is exactly the menu's, e 1 the map's */
        GfxSceneParams lib = sp;
        library_lighting(&lib, sp.time);
        const float k = e * e * (3.0f - 2.0f * e);
        lighting_mix(&sp, &lib, &sp, k);
        gfx_set_scene(app->gfx, &sp);
    }
    g->view_proj = HMM_MulM4(proj, view);
    if (g->stale_eye_armed && g->move_state == 2 && g->move.t > 0.3f) {
        /* the canary: the camera is not reported for one 60 Hz frame (as a stale eye would be); held for 1/60 s, not one loop
         * iteration: at 120 fps one iteration is shorter than the half frame the jump rule needs to judge a step, so a single
         * stale iteration merged into the next step and the canary passed or failed by luck */
        g->stale_eye_armed = false;
        g->stale_eye_until = app->time + 1.0 / 60.0;
    } else if (app->time >= g->stale_eye_until) {
        g->last_eye = eye;
    }
    g->last_cam_dist = c.dist;
    const AvatarLook hero = avatar_look_from_skin(g->profile.skin);
    g->lib.view_eye = g->last_eye;
    g->lib.view_proj = g->view_proj, g->lib.has_vp = true;
    g->lib.lod_from_rest = true; /* the move: the people keep the menu's LODs (no switch while the camera flies) */
    gfx_allow_fade(app->gfx, true); /* the shell's true fade: the move only */
    menu_library_draw(&g->lib, app->gfx, &g->avatar_meshes, &hero, dt);
    gfx_allow_fade(app->gfx, false);
    g->lib.lod_from_rest = false;
    if (g->back_loop.want > 0 && g->move_state == 2) { /* --lib-back-loop: the camera per frame of the move's own clock */
        const int f = g->back_loop.frame++;
        const float mdt = (g->move.t - g->back_loop.prev_t) * g->move.duration;
        /* the move's continuity in camera distances per second: a start 800 m out (an island's overview) moves 8x the
         * metres of one 100 m out on the same smooth curve; the 0.6 margin of the menu's ~10 m becomes 0.06 */
        const float speed = f > 0 && mdt > 1e-5f ? HMM_LenV3(HMM_SubV3(eye, g->back_loop.prev_eye)) / mdt / mg_maxf(c.dist, 1e-3f) : 0.0f;
        if (f < 10) {
            mg_log_info("back %d frame %d: frame_dt %.4f move t %.4f e %.4f eye %.3f %.3f %.3f dist %.1f speed %.4f dist/s", g->back_loop.done, f, (double)app->frame_dt,
                        (double)g->move.t, (double)e, (double)eye.X, (double)eye.Y, (double)eye.Z, (double)c.dist, (double)speed);
        }
        const bool judged = f == 0 || mdt >= 0.5f / 60.0f; /* as the library test: at least half a frame of the move's clock */
        if (judged && f > 1 && speed > 1.5f * g->back_loop.prev_speed + 0.06f) {
            const float jump = speed - 1.5f * g->back_loop.prev_speed;
            g->back_loop.worst = mg_maxf(g->back_loop.worst, jump);
            mg_log_info("back %d: JUMP at frame %d: %.4f dist/s after %.4f (frame_dt %.4f)", g->back_loop.done, f, (double)speed, (double)g->back_loop.prev_speed,
                        (double)app->frame_dt);
            g->back_loop.bad |= 1 << 30;
        }
        if (judged) {
            g->back_loop.prev_eye = eye, g->back_loop.prev_t = g->move.t;
            if (f > 0) {
                g->back_loop.prev_speed = speed;
            }
        }
    }
    const double age = app->time - g->move_t0;
    if (age < k_ui.exit_s) { /* the screen we left plays its exit: slides back, fades */
        const float u = (float)(age / k_ui.exit_s);
        gfx_ui_screen(app->gfx, -24.0f * u, 0.0f, 1.0f - u);
        if (g->move_state == 1) {
            FrontendCtx ctx;
            SDL_zero(ctx);
            ctx.name = g->profile.display_name, ctx.skin = &g->profile.skin, ctx.world_behind = true, ctx.heads = &g->heads, ctx.version = MG_VERSION;
            ctx.lobby_visible = g->lobby_visible, ctx.rows = g->menu_rows, ctx.row_count = g->menu_row_count, ctx.maps_tag = g->maps_tag, ctx.has_save = g->has_save;
            ctx.maps_ready = true;
            (void)frontend_frame(&g->fe, &g->ui, app, &ctx, &g->avatar_meshes);
        }
    }
    if (done) {
        g->wm.cam_override = false;
        g->wm.hud_off = false;
        if (g->move_state == 1) {
            g->wm.cam_snap = true; /* lands exactly on the map's own camera */
            g->lib.shell_k = 1.0f;
        } else {
            g->lib.shell_k = 0.0f;
            g->ui_screen_key = -1; /* the menu's UI enters */
        }
        mg_log_info("library: move %s done in %.2f s", g->move_state == 1 ? "to the map" : "back", age);
        if (g->back_loop.want > 0 && g->move_state == 2) {
            const bool jumped = (g->back_loop.bad & (1 << 30)) != 0;
            g->back_loop.bad = (g->back_loop.bad & ~(1 << 30)) + (jumped ? 1 : 0);
            g->back_loop.done++;
            g->back_loop.frame = 0, g->back_loop.prev_speed = 0.0f;
            if (g->back_loop.done >= g->back_loop.want) {
                mg_log_info("back loop: %d of %d Back moves clean, worst jump %.4f camera distances/s", g->back_loop.done - g->back_loop.bad, g->back_loop.done,
                            (double)g->back_loop.worst);
                app->quit_requested = true;
            }
        }
        g->move_state = 0;
    }
}

/* FE_MAP: the island instead of the menu's backdrop */
static void map_flow(Game *g, MgApp *app)
{
    (void)menu_world(g, app); /* off: the hospital's crowd stops */
    map_preload(g, app);       /* places the library when the map opened without the menu (--frontend books|map) */
    GfxSceneParams sp = {0};
    scene_defaults(&sp, (float)app->time, &g->day_state);
    g->wm.join_disabled = lobby_is_guest(&g->lobby); /* only the host starts the game */
    if (g->perf.active && SDL_strcmp(g->perf.scenario, "worldmap_travel") == 0 && g->wm.state == WM_ARRIVED) {
        worldmap_go(&g->wm, wm_cur(&g->wm)->selected + 1); /* on to the next hospital, around the island */
    }
    if (g->perf.active && SDL_strcmp(g->perf.scenario, "worldmap_voyage") == 0 && g->wm.state == WM_BOOKS) {
        g->prefs.world.rides.forced = RIDE_PLANE; /* back and forth by plane */
        worldmap_voyage(&g->wm, (g->wm.cur + 1) % g->wm.count);
    }
    const WmAction a = worldmap_frame(&g->wm, &g->ui, app, &sp, g->frame_alpha);
    g->last_eye = g->wm.eye;
    if (g->lib_placed) { /* the library stands on the island played last: the closed building */
        g->lib.shell_k = 1.0f;
        g->lib.view_eye = g->last_eye;
    g->lib.view_proj = g->view_proj, g->lib.has_vp = true;
        menu_library_draw(&g->lib, app->gfx, &g->avatar_meshes, NULL, 0.0f);
    }
    if (g->prefs.world.rides.unsynced) { /* a trip drew a vehicle: the account's copy follows */
        g->prefs.world.rides.updated_ms = wall_clock_ms();
        push_rides(g);
    }
    for (int i = 0; i < g->wm.count; i++) { /* every island keeps its ambulance where it is */
        world_book_state(&g->prefs.world, g->wm.isles[i].book->id)->ambulance_s = g->wm.isles[i].amb_s;
    }
    draw_notices(g, app);
    frontend_toast_draw(&g->fe, &g->ui, app);
    if (a == WM_BACK && g->lib_placed && !g->move_from_pause && motion_reduced(g)) { /* Reduse: the cross-dissolve */
        save_prefs(g);
        gfx_dissolve_capture(app->gfx);
        g->dissolve_t0 = app->time;
        g->lib.shell_k = 0.0f;
        frontend_back(&g->fe);
    } else if (a == WM_BACK && g->lib_placed && !g->move_from_pause) { /* the same path back onto the studying hero */
        begin_back_move(g, app);
        frontend_back(&g->fe);
    } else if (a == WM_BACK) {
        save_prefs(g);
        frontend_back(&g->fe);
    } else if (a == WM_JOIN) {
        join_level(g, app, wm_cur(&g->wm)->selected);
    }
}

/* Perf scenario ui_juice: every 2 s a toast enters and a T3 reward (flight of 40 into a counter, then the
 * banner, burst 26, ring, shake) plays over the menu and its lobby panel. */
static void ui_juice(Game *g, MgApp *app)
{
    const int cycle = (int)((app->time - g->start_time) / 2.0);
    const float cx = app->width_pt * 0.5f - 40.0f, cy = mg_maxf(app->safe_top, 8.0f) + 60.0f;
    ui_counter(&g->ui, "juice_pct", cx, cy, 40.0f, "", 40 * g->juice_cycles, "%", &g->juice_shown);
    if (cycle > g->juice_cycles) {
        g->juice_cycles = cycle;
        frontend_toast(&g->fe, str("link_ok"), app->time);
        ui_fx_tier(&g->ui_fx, UI_TN, app->width_pt * 0.5f, 40.0f, true);
        ui_fx_reward(&g->ui_fx, app->width_pt * 0.5f, app->height_pt * 0.6f, ui_key("juice_pct"), cx + 40.0f, cy + 17.0f, 40);
        g->juice_banner_at = app->time + 1.2;
    }
    if (g->juice_banner_at > 0.0 && app->time >= g->juice_banner_at) {
        g->juice_banner_at = 0.0;
        ui_fx_banner(&g->ui_fx, str("wm_completed"));
        ui_fx_tier(&g->ui_fx, UI_T3, app->width_pt * 0.5f, app->height_pt * 0.5f, true);
    }
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
    if (g->dissolve_t0 > 0.0) { /* LIB-1 Reduse: the captured frame fades out over the new one */
        const float a = 1.0f - (float)((app->time - g->dissolve_t0) / DISSOLVE_S);
        gfx_dissolve(app->gfx, a);
        if (a <= 0.0f) {
            g->dissolve_t0 = 0.0;
        }
    }
    if (g->move_state != 0) { /* LIB-1: the camera move between the library and the map */
        move_frame(g, app);
        return;
    }
    if (f->state != FE_MAP) {
        map_preload(g, app);
    }
    if (g->move_demo == 4 && g->wm.loaded && app->time - g->move_t0 > (double)g->lib.sc.transition.duration_s + 0.5) { /* menu_to_map */
        if (f->state == FE_MAIN_MENU) {
            begin_map_move(g, app, WM_ARRIVED);
        } else if (f->state == FE_MAP) {
            begin_back_move(g, app);
            frontend_back(f);
        }
        return;
    }
    if (g->move_demo == 1 && f->state == FE_MAIN_MENU && g->wm.loaded && app->time > 3.0) {
        g->move_demo = 2;
        begin_map_move(g, app, WM_ARRIVED);
        return;
    }
    if (g->move_demo == 2 && f->state == FE_MAP && app->time > 9.0) {
        g->move_demo = 3;
        begin_back_move(g, app);
        frontend_back(f);
        return;
    }
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
    if (g->lib_demo_ctl >= 0 && f->state != FE_MAP) { /* --autotest library: the scenario the script asks for */
        static const char *const ids[3] = {"me", "friend", "hostA"};
        static const char *const names[3] = {"Petre", "Ana", "Mihai"};
        const int sc = g->lib_demo_ctl;
        g->lobby_visible = true;
        g->menu_row_count = sc == 0 ? 1 : sc == 1 ? 2 : 3;
        for (int i = 0; i < g->menu_row_count; i++) {
            LobbyRow *r = &g->menu_rows[i];
            SDL_zerop(r);
            /* scenario 2: hostA (host), mateB, me */
            const char *id = sc == 2 ? (i == 0 ? "hostA" : i == 1 ? "mateB" : "me") : ids[i];
            SDL_snprintf(r->id, sizeof(r->id), "%s", id);
            SDL_snprintf(r->name, sizeof(r->name), "%s", sc == 2 ? (i == 0 ? names[2] : i == 1 ? "Ioana" : names[0]) : names[i]);
            r->skin = AVATAR_DEFAULT_SKIN;
            r->skin.suit = (uint8_t)(i * 5 + sc), r->skin.hair = (uint8_t)(2 + i), r->skin.accessory = 0;
            r->me = SDL_strcmp(id, "me") == 0;
            r->host = sc == 2 ? i == 0 : r->me;
        }
    } else if (g->lib_demo_join && f->state == FE_MAIN_MENU) { /* LIB-1 tests: a friend walks in, studies, walks out */
        if (g->lib_demo_t0 <= 0.0) {
            g->lib_demo_t0 = app->time;
        }
        const double t = app->time - g->lib_demo_t0;
        g->lobby_visible = true;
        g->menu_row_count = t > 1.0 && t < 10.0 ? 2 : 1;
        for (int i = 0; i < 2; i++) {
            LobbyRow *r = &g->menu_rows[i];
            SDL_zerop(r);
            const char *fid = arg_value(app->argc, app->argv, "--lib-demo-id"); /* desktop: the friend's id (its own table: its seed's bay) */
            SDL_snprintf(r->id, sizeof(r->id), "%s", i == 0 ? "me" : fid ? fid : "friend");
            SDL_snprintf(r->name, sizeof(r->name), i == 0 ? "Petre" : "Ana");
            r->skin = AVATAR_DEFAULT_SKIN;
            r->skin.suit = (uint8_t)(i * 5), r->skin.hair = (uint8_t)(2 + i), r->skin.accessory = 0;
            r->host = r->me = i == 0;
        }
    }
    if (f->state == FE_MAP) {
        map_flow(g, app);
        return;
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
    ctx.maps_tag = g->maps_tag;
    ctx.maps_ready = g->wm.loaded || g->wm_tried; /* LIB-1: preloaded (or failed: Maps says so) */
    ctx.lobby_visible = g->lobby_visible;
    ctx.lobby_host = (g->demo_lobby || g->lib_demo_join || lobby_is_host(&g->lobby)) && g->lib_demo_ctl != 2; /* the demo lobbies: I host */
    ctx.lobby_open = g->lobby.open;
    ctx.rows = g->menu_rows;
    ctx.row_count = g->menu_row_count;
    memcpy(ctx.slots, g->menu_slots, sizeof(ctx.slots));
    g->ui.input_off = g->ui.input_off || g->uitest.screen; /* --autotest ui: only the test screen takes touches */
    FeAction a = frontend_frame(f, &g->ui, app, &ctx, &g->avatar_meshes);
    if (g->uitest.screen) {
        g->ui.input_off = account_link_active(&g->link);
        ui_test_draw(&g->uitest, &g->ui, app);
    }
    if (g->perf.active && SDL_strcmp(g->perf.scenario, "ui_juice") == 0 && f->state == FE_MAIN_MENU) {
        ui_juice(g, app);
    }
    if (g->demo_lobby && !g->perf.active && f->state == FE_MAIN_MENU && app->time - g->start_time > 3.0 && g->start_time > 0.0) {
        lobby_layout_check(g, app, &ctx);
    }
    gfx_ui_depth(app->gfx, 0.0f, 0); /* TILT-1: the lobby players' tags are world-anchored */
    if (f->state == FE_MAIN_MENU && world_behind) { /* LIB-1: a chip over each friend at the table, none over me */
        for (int i = 1; i < LIB_SEATS; i++) {
            HMM_Vec3 top;
            const char *name;
            bool host;
            if (!menu_library_chip(&g->lib, &g->avatar_meshes, i, &top, &name, &host)) {
                continue;
            }
            const HMM_Vec4 c = HMM_MulM4V4(g->view_proj, HMM_V4V(top, 1.0f));
            if (c.W <= 0.01f) {
                continue;
            }
            const float x = (c.X / c.W * 0.5f + 0.5f) * app->width_pt, y = (0.5f - c.Y / c.W * 0.5f) * app->height_pt;
            const float half = text_width(&app->font, name, 12.0f) * 0.5f + 18.0f;
            const bool on_panel = g->lobby_visible && x + half > g->fe.lobby_x && y > g->fe.lobby_y - 12.0f && y < g->fe.lobby_y + g->fe.lobby_h + 12.0f;
            if (on_panel || x - half < frontend_menu_right(app, FE_MAIN_MENU)) {
                continue; /* walking behind the lobby panel or the menu column: no chip over the UI */
            }
            if (host) {
                frontend_draw_crown(app->gfx, &app->font, x - half + 2.0f, y - 6.0f, 12.0f);
            }
            name_tag(g, app, top, name);
        }
    }
    gfx_ui_depth(app->gfx, k_ui.tilt_depth[1], 0);
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
    case FE_OPEN_MAPS: { /* the island's overview after an Erase progress (no hospital played yet), else parked at the last one */
        g->move_from_pause = false;
        const WorldBookState *bs = world_book_state(&g->prefs.world, g->books.books[last_book_index(g)].id);
        begin_map_move(g, app, bs->last_level[0] ? WM_ARRIVED : WM_BOOKS);
        break;
    }
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
    bool view_only; /* BOOK-ENGINE: only the view storey (the institute's people) */
} CrowdCull;

/* storey + frustum + LOD, on the job system: reads the crowd, writes `out` only */
static void crowd_cull_job(void *ctx, int first, int last, int worker)
{
    (void)worker;
    const CrowdCull *c = (const CrowdCull *)ctx;
    for (int i = first; i < last; i++) {
        const npc_hot_t *h = &c->hot[i];
        c->out[i] = 255;
        if ((int)h->storey > c->floor || (c->view_only && (h->storey < 0 ? 0 : (int)h->storey) != c->floor)) {
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
        avatar_animate(&g->avatar_meshes, &cold->anim, &pose, dt);
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

/* How many NPCs the play scene declares: the hospital its roster (the targets of compute_targets), the institute the pack's
 * people (0 without a pack); -1 with no scene. The two-instance test fails when the sim does not hold that many. */
static int scene_npcs_declared(const Game *g)
{
    if (!g->scene) {
        return -1;
    }
    if (scene_ambient_npcs(g->scene)) {
        int n = 0;
        for (int a = 0; a < NPC_ARCHETYPES; a++) {
            n += g->npcs.target[a];
        }
        return n;
    }
    const char *bump = SDL_getenv("MG_TEST_NPC_DECLARE"); /* tests only: a wrong declaration must fail the two-instance test */
    return (g->bp_ok ? (int)g->book.person_count : 0) + (bump ? SDL_atoi(bump) : 0);
}

/* One sim tick, plus what the tests watch */
static void npc_step(Game *g, MgApp *app)
{
    const size_t mark = arena_mark(&app->frame);
    npc_tick(&g->npcs, app->jobs, &app->frame, MG_TICK_DT);
    if (g->bp_ok) {
        book_people_tick(&g->bp, MG_TICK_DT);
    }
    arena_reset_to(&app->frame, mark);
    if (g->npc_sync_log && g->playing && g->npcs.tick % 600u == 0u) { /* not the menu's backdrop */
        mg_log_info("npc-sync: tick %u hash %08x alive %d seed %u", g->npcs.tick, npc_world_hash(&g->npcs), g->npcs.pool.count, g->npcs.seed);
        g->npc_sync_tick = g->npcs.tick;
        g->npc_sync_hash = npc_world_hash(&g->npcs);
    }
    if (g->npc_pose_log_tick && g->playing && g->npcs.tick == g->npc_pose_log_tick) { /* finding NPCs in a pose for pictures */
        static const char *const poses[] = {"stand", "sit", "bed", "mop", "crouch"};
        const NpcPool *p = &g->npcs.pool;
        const npc_hot_t *hot = p->hot[p->read];
        for (int i = 0; i < p->count; i++) {
            const npc_hot_t *h = &hot[i];
            const npc_cold_t *c = &p->cold[i];
            mg_log_info("npc-pose: tick %u id %u %s storey %d at %.2f %.2f %.2f seat %.2f %.2f yaw %.2f sit %.2f age %d %s", g->npcs.tick, c->id,
                        poses[c->pose < 5 ? c->pose : 0], (int)h->storey, (double)h->x, (double)h->y, (double)h->z, (double)c->seat_x,
                        (double)c->seat_z, (double)h->yaw, (double)c->sit_t, (int)c->look.age, g->npcs.data.archetypes[c->archetype].name);
        }
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
        npcs_start(g, g->npcs.seed);
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
        if (g->settings.ui_sounds) {
            audio_cue(g->audio, AUDIO_CUE_TICK);
        }
        ui_haptic(HAPTIC_INTERACT);
    }
}

static void lift_panel_press(Game *g, Elevator *el, int floor)
{
    if (floor != el->car_floor && g->lift_panel.pressed < 0) {
        audio_ding(g->audio);
    }
    ui_haptic(HAPTIC_LIFT_KEY); /* a mechanical click, firmer than a UI button */
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
            const char *book_line = NULL;
            if (g->bp_ok && book_people_of_npc(&g->bp, i)) {
                if (book_people_interact(&g->bp, i, &book_line)) { /* a cast person: the missions layer talks */
                    c->talk_t = 3.0f;
                    return;
                }
            }
            const int n = g->npcs.data.n_greetings[c->archetype];
            const char *line = book_line ? book_line : n ? g->npcs.data.greetings[c->archetype][(g->greet_count++ + c->id) % (uint32_t)n] : "";
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
    g->ia_ok = g->preview_ok && ia_init(&g->ia, &g->level); /* per level; the preview card's meshes are shared */
    g->npcs.on_spawned = npc_registered;
    g->npcs.on_despawned = npc_unregistered;
    g->npcs.callback_user = g;
    for (int i = 0; i < MARKER_MAX; i++) {
        g->marker_ia[i] = IA_NONE;
    }
    const char *names[LIFT_PANEL_FLOORS];
    const int nf = SDL_min(scene_floor_count(g->scene), LIFT_PANEL_FLOORS);
    for (int f = 0; f < nf; f++) {
        names[f] = scene_floor_name(g->scene, f);
    }
    g->lift_panel_ok = lift_count(g) > 0 && elevator_panel_init(&g->lift_panel, app->gfx, &g->level, nf, &app->font, names);
    if (!g->ia_ok) {
        return;
    }
    const preview_desc_t any = {g->unit_box, NULL, 0.3f, 0.0f, 0.0f, HMM_V4(0.4f, 0.8f, 0.9f, 1.0f)};
    g->ia_any_slot = ia_add(&g->ia, IA_BUTTON, HMM_V3(0, -100.0f, 0), 0.1f, 99, IA_DISABLED | IA_FACING_FREE, 0, "Mesh", NULL, NULL, any);
    g->ia_check = SDL_strcmp(arg_value(app->argc, app->argv, "--autotest") ? arg_value(app->argc, app->argv, "--autotest") : "", "interact") == 0;
    g->ia_hover_slot = IA_NONE;
}

/* BOOK-ENGINE S4: the missions over the pack when the level is the institute: the pack's people, the script player,
 * the load-time data check. Off (no pill, no journal row) anywhere else or without a pack with missions. */
static void missions_setup(Game *g, MgApp *app)
{
    (void)app;
    missions_ui_init(&g->mui);
    g->mission_marker = 0;
    g->people = NULL;
    g->menu.journal_row = false;
    if (!g->scene || scene_kind(g->scene) != SCENE_INSTITUTE || !g->book.ok || g->book.mission_count == 0 || !g->progress_uid[0]) {
        return;
    }
    IMap map;
    if (g->nav && imap_open_embedded(&map)) { /* every step against the map and the baked grid, once per load */
        const HMM_Vec3 at = scene_spawn(g->scene, 0);
        g->bcheck = book_check(&g->book, &map, g->nav, at, scene_indoors(g->scene, at) ? scene_floor_of(g->scene, at) : -1);
    }
    g->people = g->bp_ok ? &g->bp : NULL; /* the pack's people (book_people.c) */
    missions_init(&g->missions, &g->book, &g->progress, g->people, &g->sp, &g->level);
    g->menu.journal_row = g->missions.on;
    for (uint32_t c = 0; g->missions.on && c < g->book.cast_count && c < HEAD_ICON_CELLS / 2; c++) { /* portraits: rendered in the first frames */
        const NetSkin skin = missions_cast_skin(&g->book, (int)c);
        head_icon_get(&g->heads, &skin);
    }
}

/* A dialogue, a script or the journal is up: the stick, the HUD and the interact panel step aside. */
static bool book_modal(const Game *g) { return g->mui.journal || missions_dialogue_open(&g->missions) || missions_playing(&g->missions); }

static void mission_marker_drop(Game *g)
{
    if (g->mission_marker && g->active_marker == g->mission_marker) {
        autonav_clear(&g->autonav);
        g->active_marker = 0;
    }
    if (g->mission_marker) {
        marker_remove(&g->markers, g->mission_marker);
    }
    g->mission_marker = 0;
}

/* Once per play frame: the objective, its events (progress saved and pushed when a step ends, the reward), and the
 * objective marker: one, the chapter colour, on whoever the objective is (it moves when they move). */
/* Where auto-nav walks to reach the objective marker's person: the nearest cell within talking range that the player
 * can reach (a presenter behind a counter is spoken to across it); the marker itself stays on the person. */
static HMM_Vec3 mission_walk_pos(Game *g, HMM_Vec3 at, int storey)
{
    const HMM_Vec3 p = g->player.kin.pos;
    HMM_Vec3 cell;
    float d;
    if (g->nav && book_talk_cell(g->nav, p, scene_indoors(g->scene, p) ? scene_floor_of(g->scene, p) : -1, storey, at.X, at.Z, &cell, &d)) {
        return HMM_V3(cell.X, at.Y, cell.Z);
    }
    return at;
}

static void missions_frame(Game *g, MgApp *app)
{
    Missions *m = &g->missions;
    if (!m->on || !g->scene) {
        return;
    }
    missions_update(m, wall_clock_ms());
    const uint32_t ev = missions_take_events(m);
    if (ev & ME_COMMIT) {
        progress_commit(g, true);
    }
    if (ev & ME_REWARD) { /* T3 */
        ui_fx_banner(&g->ui_fx, str((ev & ME_ARC_DONE) ? "arc_completed" : "case_completed"));
        ui_fx_tier(&g->ui_fx, UI_T3, app->width_pt * 0.5f, app->height_pt * 0.5f, true);
    }
    if (ev & ME_NO_PATIENT) {
        frontend_toast(&g->fe, str("mission_no_patient"), app->time);
    }
    const MissionObjective *o = &m->obj;
    if (o->kind == MOBJ_NONE || o->cast < 0) {
        mission_marker_drop(g);
        return;
    }
    const HMM_Vec3 pos = HMM_V3(o->pos.X, scene_floor_y(g->scene, o->storey), o->pos.Z);
    const uint32_t col = missions_case_color(m, o->mission);
    marker_t *mk = marker_get(&g->markers, g->mission_marker);
    if (!mk) {
        g->mission_marker = marker_add(&g->markers, pos, o->storey, MARKER_OBJECTIVE, col);
        return;
    }
    const float dx = mk->pos.X - pos.X, dz = mk->pos.Z - pos.Z;
    const bool moved = dx * dx + dz * dz > 0.25f * 0.25f || mk->storey != o->storey;
    mk->pos = pos;
    mk->storey = (int8_t)o->storey;
    mk->color_rgba = col;
    if (moved && g->active_marker == mk->id && autonav_active(&g->autonav) && g->nav) { /* walking to them: follow */
        const HMM_Vec3 p = g->player.kin.pos;
        AutonavCtx c = {p, scene_indoors(g->scene, p) ? scene_floor_of(g->scene, p) : -1, app->time, false, false, 0};
        autonav_go(&g->autonav, g->nav, &c, mission_walk_pos(g, pos, o->storey), o->storey, mk->id);
    }
}

/* The journal page (from the pause card), the script player and the dialogue card. */
static void missions_overlays(Game *g, MgApp *app)
{
    if (!g->missions.on) {
        return;
    }
    if (g->mui.journal) {
        const MissionUiAction a = missions_journal(&g->mui, &g->missions, &g->ui, app, &g->heads);
        if (a == MUI_CLOSE_JOURNAL) { /* back to the pause card */
            g->mui.journal = false;
            g->menu.open = g->menu.card = true;
        } else if (a == MUI_TAKE_ME) { /* tracked already: walk to the objective */
            g->mui.journal = false;
            g->menu.open = g->menu.card = false;
            missions_frame(g, app);
            if (g->mission_marker && g->nav) {
                marker_tapped(g, app, g->mission_marker);
            }
        }
        return;
    }
    if (g->menu.open || g->fpage.open) {
        return;
    }
    missions_script_frame(&g->missions, &g->ui, app); /* a mission step's script (it replies and commits when it ends: ME_COMMIT) */
    missions_dialogue_card(&g->mui, &g->missions, &g->ui, app, &g->heads);
    g->mui.pill[2] = 0.0f;
    if (!book_modal(g)) { /* the objective pill under the account chip (under the stats when they show); drawn before the stick takes
                           * touches, so a tap on it is the pill's */
        const ui_frame_t cf = hud_frame(app);
        float cpx, cpy, cps;
        ui_pause_rect(&cf, &cpx, &cpy, &cps);
        const float chip_l = ui_safe_left(&cf) + UI_MARGIN + cf.corner;
        const float py = cpy + CHIP_H + 8.0f + (g->settings.show_stats && !g->perf.active ? 98.0f : 0.0f);
        const float max_w = mg_minf(app->width_pt * 0.42f, cpx - UI_MARGIN - chip_l - 140.0f);
        if (missions_pill(&g->mui, &g->missions, &g->ui, chip_l, py, mg_maxf(max_w, 200.0f)) == MUI_OPEN_JOURNAL) {
            g->mui.journal = true;
            g->menu.open = g->menu.card = true;
            g->menu.from_card = false;
            if (g->missions.tracked >= 0) {
                g->mui.arc_tab = SDL_max(missions_arc_of_chapter(&g->book, g->book.missions[g->missions.tracked].chapter), 0);
            }
        }
    }
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
    const Elevator *near_lift = lift_get(g, lift_nearest(g, g->player.kin.pos)); /* the scene's lifts, not the room graph's one car */
    pr->lift_at = near_lift ? HMM_V3((near_lift->x0 + near_lift->x1) * 0.5f, 0.1f, (near_lift->z0 + near_lift->z1) * 0.5f)
                            : HMM_V3((g->rooms.lift[0] + g->rooms.lift[2]) * 0.5f, 0.1f, (g->rooms.lift[1] + g->rooms.lift[3]) * 0.5f);
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
    for (int f = 0; f < g->lift_panel.floors && f < LIFT_PANEL_FLOORS && g->lift_panel_ok; f++) {
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
        const BookPerson *bper = g->bp_ok ? book_people_of_npc(&g->bp, i) : NULL;
        if (bper) { /* BOOK-ENGINE: cast people always talk (the objective's asks to), the pack says who else greets */
            if (chat_eval) {
                const unsigned f = book_people_ia_flags(&g->bp, i);
                c->wants_chat = (f & IA_WANTS_CHAT) != 0;
                ia_set_flags(&g->ia, c->ia_slot, f);
            }
            ia_set_pos(&g->ia, c->ia_slot, HMM_V3(hot[i].x, hot[i].y, hot[i].z), hot[i].storey);
            g->ia.cold[c->ia_slot].label = bper->name ? bper->name : c->name;
            g->ia.cold[c->ia_slot].sublabel = bper->role;
            g->ia.cold[c->ia_slot].preview.look = &c->look;
            g->ia.hot[c->ia_slot].ref = (uint16_t)c->id;
            continue;
        }
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
            if (m->id && m->id != g->mission_marker) {
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
    const UiColor white = {255, 255, 255, alpha}; /* the surface colour */
    ui_round_rect(app->gfx, &app->font, x - 19.0f * k, y - 12.0f * k, 38.0f * k, 24.0f * k, 12.0f * k, white); /* one shape: overlapping ones show through each other */
    for (int d = 0; d < 3; d++) {
        const float phase = active ? fmodf(t / 0.9f + (float)d / 3.0f, 1.0f) : 0.0f; /* in sequence */
        const float r = (active ? 1.8f + 1.4f * HMM_SinF(phase * HMM_PI32) : 2.0f) * k;
        const UiColor dot = {0x17, 0x3A, 0x3A, alpha}; /* ink */
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
        /* the head's top: lying on the pillow, seated, standing (LIB-1 avatar; children x 0.72) */
        const float top = (c->pose == POSE_BED ? 1.25f : (c->pose == POSE_SIT ? 1.66f : 1.6f)) * (c->look.age == AVATAR_CHILD ? 0.72f : 1.0f);
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
    CrowdCull cull = {hot, lods, g->view_proj, g->player.kin.pos.X, g->player.kin.pos.Z, near_m * near_m, floor, g->no_lod, g->bp_ok};
    if (g->bp_ok) {
        book_people_set_view(&g->bp, floor, &g->view_proj);
        g->bp_drawn = 0;
        g->bp_drawn_floor = floor;
    }
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
        if (c->pose == POSE_SIT) { /* the body base on the seat's top (+ the library's clearance) */
            const bool bench = c->slot != SLOT_NONE && c->slot < (uint32_t)g->rooms.slot_count && (g->rooms.slots[c->slot].tags & SLOT_BENCH);
            pose.seat_y = (c->pose_y > 0.0f ? c->pose_y : bench ? HOSP_BENCH_TOP : HOSP_CHAIR_TOP) + g->avatar_meshes.rig.seat_clearance;
        }
        if (c->pose == POSE_BED && c->pose_y > 0.0f) { /* BOOK-ENGINE: the record's mattress (the lying pose is set on a 0.6 m one) */
            pose.feet.Y += (c->pose_y - 0.6f) * c->sit_t;
        }
        pose.lying = c->pose == POSE_BED ? c->sit_t : 0.0f;
        c->vis_talk += ((c->speaking ? 1.0f : 0.0f) - c->vis_talk) * mg_minf(dt * 6.0f, 1.0f);
        pose.talk = c->vis_talk;
        pose.crouch = c->pose == POSE_CROUCH ? 1.0f : 0.0f;
        pose.sway = c->pose == POSE_MOP ? 0.12f * HMM_SinF((float)app->time * 3.0f + (float)i) : 0.0f;
        pose.hide = c->fade < 1.0f ? 1.0f - c->fade : 0.0f;
        if (g->menu_world_on) { /* the hero stays the nearest thing: anyone coming closer fades out first */
            pose.hide = mg_maxf(pose.hide, mg_clampf((feet.Z - (MENU_HERO_Z - 1.0f)) / 1.5f, 0.0f, 1.0f));
        }
        npc_idle_touches(g, app, i, h, c, &pose, dt, queue_waiting);
        pose.head_lift = mg_maxf(pose.head_lift, 0.08f * c->vis_talk); /* speaking: the head lifts a little too */
        avatar_animate(&g->avatar_meshes, &c->anim, &pose, dt);
        avatar_draw(app->gfx, &g->avatar_meshes, &c->look, &pose, &c->anim, NULL);
        g->rendered_entities++;
        g->bp_drawn += g->bp_ok;
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

/* Developer -> Tilt debug: the raw gravity, the mapped screen values, the tilt, and a test card with arrows for
 * where the layers shift and where its lip swings, so an inverted axis shows in a screenshot. Debug builds. */
static void tilt_debug_draw(Game *g, MgApp *app)
{
#ifndef NDEBUG
    if (!g->settings.show_tilt_debug) {
        return;
    }
    GfxContext *gfx = app->gfx;
    const float x0 = app->safe_left + 200.0f, y0 = app->safe_top + 110.0f;
    char line[160];
    gfx_ui_depth(gfx, 0.0f, 0);
    ui_round_rect(gfx, &app->font, x0 - 10.0f, y0 - 10.0f, 360.0f, 104.0f, 12.0f, ui_rgba(0x101318D0));
    if (g->tilt_have) {
        SDL_snprintf(line, sizeof(line), "gravity  x %+.2f  y %+.2f  z %+.2f  (%s)", (double)g->tilt_raw[0], (double)g->tilt_raw[1], (double)g->tilt_raw[2],
                     g->tilt_flipped ? "landscape flipped" : "landscape");
    } else {
        SDL_snprintf(line, sizeof(line), "gravity  none (%s)", g->tilt.forced ? "forced: --debug tilt / T + mouse" : "no sensor or S = 0");
    }
    text_draw(gfx, &app->font, x0, y0, 13.0f, ui_rgba(0xFFFFFFFF), line);
    SDL_snprintf(line, sizeof(line), "angle %+.1f %+.1f  ref %+.1f %+.1f  still %.2f s%s", (double)g->tilt.ax, (double)g->tilt.ay, (double)g->tilt.refx,
                 (double)g->tilt.refy, (double)g->tilt.still_s, g->tilt.at_rest ? "  at rest" : "");
    text_draw(gfx, &app->font, x0, y0 + 22.0f, 13.0f, ui_rgba(0xFFFFFFFF), line);
    SDL_snprintf(line, sizeof(line), "target   %+.2f %+.2f   tilt %+.2f %+.2f   S %.1f   signs %+.0f %+.0f", (double)g->tilt.target_x, (double)g->tilt.target_y,
                 (double)g->tilt.tx, (double)g->tilt.ty, (double)g->tilt_s, (double)k_ui.tilt_sign_x, (double)k_ui.tilt_sign_y);
    text_draw(gfx, &app->font, x0, y0 + 44.0f, 13.0f, ui_rgba(0xFFFFFFFF), line);
    text_draw(gfx, &app->font, x0, y0 + 66.0f, 12.0f, ui_rgba(0xFFD166FF), "yellow: the layers shift   teal: the lip swings   white: tilt");
    /* the test card: a real card + button through the same pipeline */
    const float cx = x0 + 450.0f, cy = y0 + 50.0f;
    ui_depth(&g->ui, 1, 0, 0);
    ui_surface(&g->ui, cx - 70.0f, cy - 40.0f, 140.0f, 80.0f, k_ui.r_card * 0.6f, k_ui.lip_card, ui_rgba(k_ui.surface), ui_rgba(k_ui.lip_mint), 0.0f, ui_rgba(0));
    ui_depth(&g->ui, 2, 0, GFX_TILT_GLOSS);
    ui_surface(&g->ui, cx - 40.0f, cy - 14.0f, 80.0f, 28.0f, 14.0f, k_ui.lip_button, ui_rgba(k_ui.primary), ui_rgba(k_ui.primary_lip), k_ui.highlight_band, ui_rgba(k_ui.highlight));
    gfx_ui_depth(gfx, 0.0f, 0);
    const float tx = g->ui.tilt_x, ty = g->ui.tilt_y;
    struct { float dx, dy; uint32_t c; } arrows[3] = {{-tx * 50.0f, -ty * 50.0f, 0xFFD166FF}, {-0.7f * tx * 50.0f, -0.4f * ty * 50.0f, 0x12A594FF}, {tx * 50.0f, ty * 50.0f, 0xFFFFFFFF}};
    for (int i = 0; i < 3; i++) {
        const float ex = cx + arrows[i].dx, ey = cy + 70.0f + arrows[i].dy, len = sqrtf(arrows[i].dx * arrows[i].dx + arrows[i].dy * arrows[i].dy);
        ui_circle(gfx, &app->font, cx, cy + 70.0f, 3.0f, ui_rgba(arrows[i].c));
        if (len > 2.0f) {
            const float ux = arrows[i].dx / len, uy = arrows[i].dy / len;
            ui_line(gfx, &app->font, cx, cy + 70.0f, ex, ey, 2.5f, ui_rgba(arrows[i].c));
            ui_line(gfx, &app->font, ex, ey, ex - 8.0f * ux + 5.0f * uy, ey - 8.0f * uy - 5.0f * ux, 2.5f, ui_rgba(arrows[i].c));
            ui_line(gfx, &app->font, ex, ey, ex - 8.0f * ux - 5.0f * uy, ey - 8.0f * uy + 5.0f * ux, 2.5f, ui_rgba(arrows[i].c));
        }
    }
    gfx_ui_depth(gfx, k_ui.tilt_depth[1], 0);
#else
    (void)g, (void)app;
#endif
}

void game_frame(MgApp *app, float alpha)
{
    { /* the late-frame log (a tracked bug): the frame that just ended, with what it noted */
        static const char *const screens[] = {"WELCOME", "USERNAME", "CREATOR", "MAIN_MENU", "SETTINGS", "FRIENDS", "LOADING", "PLAY", "MAP"};
        const Game *gg = (const Game *)app->game;
        const int st = (int)gg->fe.state;
        frame_notes_check(app->time, (double)app->frame_dt * 1000.0, st >= 0 && st < (int)MG_ARRAY_COUNT(screens) ? screens[st] : "?",
                          gg->playing && gg->scene ? scene_name(gg->scene) : gg->menu_world_on ? "library" : "-");
    }
    game_frame_inner(app, alpha);
    Game *g = (Game *)app->game;
    tilt_debug_draw(g, app);
    gfx_ui_screen(app->gfx, 0.0f, 0.0f, 1.0f);
    {
        PROF_SCOPE("ui.fx"); /* tweens' effects: particles, rings, tokens, banner */
        gfx_ui_depth(app->gfx, k_ui.tilt_depth[4], 0); /* TILT-1: particles, tokens in flight, the banner */
        ui_fx_update_draw(&g->ui_fx, app->gfx, app->width_pt, app->height_pt); /* over every screen */
        gfx_ui_depth(app->gfx, k_ui.tilt_depth[1], 0);
    }
    head_icons_update(&g->heads, app->gfx); /* what this frame's UI asked for */
    ui_haptics_flush(); /* this frame's strongest haptic, on the frame of its input */
}

static void game_frame_inner(MgApp *app, float alpha)
{
    Game *g = (Game *)app->game;
    g->frame_alpha = alpha;
    GameSettings *s = &g->settings;
    ui_haptics_frame(app->frame_index, app->time); /* the recorder's frame and the limiter's clock */
    ui_haptic_set_level((HapticLevel)s->haptics);
    if (g->npcs_ok && g->playing) {
        /* other autotests keep the world empty, so their numbers stay comparable */
        const char *at_kind = arg_value(app->argc, app->argv, "--autotest");
        const bool roster_test = at_kind && (SDL_strcmp(at_kind, "friends") == 0 || SDL_strcmp(at_kind, "interact") == 0 || SDL_strcmp(at_kind, "npcs") == 0 || SDL_strcmp(at_kind, "npcs-save") == 0 || SDL_strcmp(at_kind, "npcs-continue") == 0 ||
                                             perf_wants_npcs(g));
        if (!at_kind || roster_test || g->bp_ok) { /* the institute's people are part of its scene (every test there has them) */
            const uint32_t seed = npc_seed_for(g, app);
            const bool want = roster_test || s->npc_density > 0.0f; /* density Low only draws fewer: every client simulates the same people */
            if (want && (!g->npcs_on || seed != g->npcs.seed || s->respawn_npcs)) {
                s->respawn_npcs = false;
                g->npcs_on = true;
                npcs_start(g, seed);
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
                           g->playing && player_lift_busy(g),
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
    probe.npc_world = g->npcs_ok;
    probe.npc_declared = scene_npcs_declared(g);
    probe.npc_alive = g->npcs_on ? g->npcs.pool.count : 0;
    probe.lib_declared = g->lib.ok ? g->lib.sc.npc_n : -1;
    probe.lib_live = g->lib.ok && g->lib.people ? g->lib.pcfg->n : 0;
    probe.show_stats = s->show_stats;
    probe.scene_kind = g->scene ? (int)scene_kind(g->scene) : -1;
    probe.lift_count = lift_count(g);
    probe.floor_count = g->scene ? scene_floor_count(g->scene) : 0;
    probe.ride_lift = g->ride_lift;
    {
        GfxStats gs;
        gfx_get_stats(app->gfx, &gs);
        probe.draws = gs.draws;
    }
    probe.frame_allocs = prof_allocs_last_frame();
    probe.book_people = g->bp_ok && g->playing ? &g->bp : NULL;
    probe.book_drawn = g->bp_drawn;
    probe.book_drawn_floor = g->bp_ok && g->npcs_on ? g->bp_drawn_floor : -1;
    probe.missions = g->missions.on ? &g->missions : NULL;
    probe.journal_open = g->mui.journal;
    probe.menu_open = g->menu.open;
    probe.sp = &g->sp;
    probe.book = &g->book;
    probe.progress = g->progress_uid[0] ? &g->progress : NULL;
    {
        GfxStats gs;
        gfx_get_stats(app->gfx, &gs);
        probe.ui_draws = gs.ui_draws;
        probe.passes = gs.passes;
    }
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
        /* BOOK-1: the pack's punctuation and Greek in every face, and the long text drawn as a screenful */
        static const uint32_t book[] = {0x201E, 0x201D, 0x2013, 0x2019, 0x203A, 0x2192, 0x2212, 0x2020, 0x3B1, 0x3B2, 0x3B3};
        for (size_t i = 0; i < MG_ARRAY_COUNT(book); i++) {
            probe.text_ok = probe.text_ok && text_has_glyph(&app->font, book[i]) && text_has_glyph(&app->font_display, book[i]) &&
                            text_has_glyph(&app->font_bold, book[i]);
        }
        probe.text_ok = probe.text_ok && g->book_area.drawn > 0 && g->book_area_inst < 3000u;
    }
    if (g->ia_check && g->ia_ok && g->playing) {
        interact_probe(g, app, &probe);
    } else {
        probe.active_slot = g->ia_ok ? (int)g->ia.active : -1;
    }
    probe.fe = &g->fe;
    probe.wm = g->wm.loaded ? &g->wm : NULL;
    probe.world_rides = &g->prefs.world.rides;
    probe.play_level = g->play_level;
    probe.uitest = &g->uitest;
    probe.net_status = g->net ? (int)net_status(g->net) : -1;
    probe.net_ready = g->net && net_socket_ready(g->net);
    probe.avatar_meshes = &g->avatar_meshes;
    probe.library = &g->lib;
    probe.move_state = g->move_state;
    probe.move_t = g->move.t;
    probe.cam_eye = g->last_eye;
    probe.cam_dist = g->last_cam_dist;
    probe.islands_drawn = g->wm.islands_drawn;
    probe.frame_dt = (float)app->frame_dt;
    probe.dissolve = g->dissolve_t0 > 0.0 ? 1.0f - (float)((app->time - g->dissolve_t0) / DISSOLVE_S) : 0.0f;
    probe.text_center_err = g->text_test ? g->text_center_err : -1.0f;
    for (int i = 0; i < 3; i++) {
        probe.text_center_each[i] = g->text_center_each[i];
    }
    probe.tilt = (TiltCtl){&g->tilt, &g->tilt_debug, &g->test_low_power, &g->tilt_debug_x, &g->tilt_debug_y, &g->tilt_s, &g->settings.ui_tilt};
    probe.stick = &g->stick_view;
    probe.touch = &g->touch;
    probe.stick_r = g->touch_cfg.radius_pt;
    stick_view_rest(&g->touch_cfg, app->safe_left, app->height_pt, app->safe_bottom, &probe.stick_rest_x, &probe.stick_rest_y);
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
    if (has_flag(app->argc, app->argv, "--log-frame-stats")) { /* counts (not times): valid on battery too */
        GfxStats st;
        gfx_get_stats(app->gfx, &st);
        g->fs_n++, g->fs_draws += st.draws, g->fs_tris += st.tris;
        for (int b = 0; b < 4; b++) {
            g->fs_vis[b] += (g->lib.visible >> b) & 1u;
        }
        for (int c = 0; c < GFX_CATS; c++) {
            g->fs_cat[c] += st.tris_cat[c];
        }
        if (g->fs_n == 120) {
            const double n = 120.0;
            mg_log_info("frame stats: %.1f fps over the last 120 frames (window %s)", g->fs_t0 > 0.0 ? 120.0 / (app->time - g->fs_t0) : 0.0,
                        (SDL_GetWindowFlags(app->window) & (SDL_WINDOW_OCCLUDED | SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) ? "occluded" : "visible");
            g->fs_t0 = app->time;
            mg_log_info("frame stats (120 frames): draws %.1f tris %.1fk (room %.1fk prop %.1fk avatar %.1fk other %.1fk)", g->fs_draws / n,
                        g->fs_tris / n / 1000.0, g->fs_cat[GFX_CAT_ROOM] / n / 1000.0, g->fs_cat[GFX_CAT_PROP] / n / 1000.0,
                        g->fs_cat[GFX_CAT_AVATAR] / n / 1000.0, g->fs_cat[GFX_CAT_OTHER] / n / 1000.0);
            mg_log_info("frame stats: bays visible %u %u %u %u of 120 frames (wall rule %s)", g->fs_vis[0], g->fs_vis[1], g->fs_vis[2], g->fs_vis[3],
                        g->lib.no_wall_vis ? "off" : "on");
            SDL_memset(g->fs_vis, 0, sizeof(g->fs_vis));
            g->fs_n = 0, g->fs_draws = g->fs_tris = 0;
            SDL_memset(g->fs_cat, 0, sizeof(g->fs_cat));
        }
    }
    if (has_flag(app->argc, app->argv, "--lib-wall-ab")) { /* test: the wall rule on for two frames, off for two (pixel compare of
                                                              * frames of the same parity: some dither alternates every frame) */
        g->lib_ab_frame++;
        g->lib.no_wall_vis = (g->lib_ab_frame >> 1) & 1u;
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
    if (ato.force_ride > 0) { /* --autotest worldmap2 */
        g->prefs.world.rides.forced = ato.force_ride - 1;
    }
    if (ato.teleport && g->playing) { /* --autotest bookpeople */
        player_init(&g->player, ato.teleport_pos, ato.teleport_yaw);
        g->cam.initialized = g->td.initialized = false;
        g->fade_snap = true;
    }
    if (ato.stale_eye_once) {
        g->stale_eye_armed = true;
    }
    if (ato.lib_phone > 0) { /* --autotest library: where the phones lie (1 pocket, 2 table) */
        menu_library_phone_place(&g->lib, ato.lib_phone == 2);
    }
    if (ato.lib_cat_hold) { /* --autotest library: no cat visits (no pet cutting into a forced action) until step 13 */
        g->lib.cat_hold = ato.lib_cat_hold == 1;
    }
    if (ato.lib_demo > 0) { /* --autotest library: the demo lobby's scenario */
        g->lib_demo_ctl = ato.lib_demo - 1;
    }
    if (ato.lib_plane == 1) { /* --autotest library: an invite sent to "friend" (its plane) */
        g->plane_at = app->time;
        SDL_snprintf(g->plane_to, sizeof(g->plane_to), "friend");
    } else if (ato.lib_plane == 2) {
        menu_library_receive_plane(&g->lib, "hostA");
    } else if (ato.lib_plane == 3) {
        menu_library_plane_close(&g->lib);
    }
    if (ato.lib_fidget > 0) { /* --autotest library: the hero plays this action now (PET through the cat's hook) */
        menu_library_fidget(&g->lib, 0, (LibAction)(ato.lib_fidget - 1));
    }
    if (ato.last_book[0]) {
        SDL_snprintf(g->prefs.world.last_book, sizeof(g->prefs.world.last_book), "%s", ato.last_book);
        maps_tag_update(g);
    }
    if (ato.save_prefs) {
        save_prefs(g);
    }
    if (ato.debug_level[0]) { /* --autotest worldmap: level-progress */
        world_debug_progress(&g->prefs.world, ato.debug_level, ato.debug_pct);
        maps_tag_update(g);
        mg_log_info("world: test progress %s = %d%%", ato.debug_level, ato.debug_pct);
    }
    if (ato.net_drop && g->net) {
        net_debug_drop(g->net);
    }
    if (ato.set_haptics > 0) {
        g->settings.haptics = ato.set_haptics - 1;
        ui_haptic_set_level((HapticLevel)g->settings.haptics);
    }
    if (ato.add_marker_left && g->playing) {
        const HMM_Vec3 at = HMM_AddV3(g->player.kin.pos, HMM_V3(-5.0f, 0.0f, 0.0f));
        marker_add(&g->markers, at, probe.nav_storey, MARKER_OBJECTIVE, 0xFF9F0AFF);
    }
    if (ato.respawn_markers) {
        marker_clear(&g->markers, MARKER_TEST);
    }
    if (ato.missions_reset && g->missions.on) { /* --autotest missions: a clean book, tracking from the first case */
        book_progress_reset(&g->progress, wall_clock_ms());
        if (g->net && SDL_strcmp(g->progress_uid, "local") != 0) { /* and the account's row, or the next merge brings it back */
            net_reset_book_progress(g->net, PROGRESS_BOOK);
        }
        progress_commit(g, false);
        g->missions.tracked = MISSION_NONE;
        g->missions.progress_seen = -1.0;
    }
    if (ato.missions_go && g->mission_marker && g->nav) {
        marker_tapped(g, app, g->mission_marker);
    }
    if (ato.missions_skip_fact > 0) { /* the next script played leaves this fact unset (answers its items wrong) */
        g->sp.skip_fact = ato.missions_skip_fact - 1;
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
    static const char *const names[] = {"WELCOME", "USERNAME", "CREATOR", "MAIN_MENU", "SETTINGS", "FRIENDS", "LOADING", "PLAY", "MAP"};
    if ((int)g->fe.state != g->logged_fe) { /* screen changes, for device logs */
        g->logged_fe = (int)g->fe.state;
        mg_log_info("frontend: %s", names[g->logged_fe]);
    }

    /* network: events, remote buffers, our state at the adaptive rate */
    {
        const KinematicState *k = &g->player.kin;
        uint8_t flags = (uint8_t)((k->grounded ? REP_GROUNDED : 0) | (k->jumping ? REP_JUMPING : 0) |
                                  (g->player.sprint ? REP_SPRINTING : 0) |
                                  (g->playing && player_lift_busy(g) ? REP_RIDING : 0));
        PROF_SCOPE("net.poll");
        friends_tick(&g->friends, app->time);
        {
            const FeState st = g->fe.state;
            lobby_set_menu(&g->lobby, !g->playing && !g->play_paused &&
                                          (st == FE_MAIN_MENU || st == FE_SETTINGS || st == FE_FRIENDS || st == FE_MAP || (st == FE_CREATOR && g->prefs.onboarding_done)));
        }
        lobby_tick(&g->lobby, app->time);
        lobby_follow(g, app);
        session_update(&g->session, app->time, app->clock.tick, k->pos, k->vel, g->player.yaw, flags,
                       (uint8_t)(g->playing ? scene_floor_of(g->scene, k->pos) : 0));
        for (int li = 0; g->playing && li < lift_count(g); li++) { /* someone riding from our landing: their doors close here too */
            Elevator *el = lift_get(g, li);
            for (int f = 0; f < el->floors; f++) {
                el->hold_shut[f] = false;
            }
            for (int i = 0; i < SESSION_MAX_REMOTE; i++) {
                const RemotePlayer *r = &g->session.remotes[i];
                /* the replication has no lift id: with several lifts, the car the rider stands in */
                if (r->active && r->visible && (r->sample.flags & REP_RIDING) && r->sample.floor < el->floors && (lift_count(g) == 1 || lift_holds_xz(el, r->pos))) {
                    el->hold_shut[r->sample.floor] = true;
                }
            }
        }
        if (g->session.spawn_pending && g->playing && g->keep_save_pos) { /* Continue: the room's spawn does not move us off the saved spot */
            g->session.spawn_pending = false;
            g->keep_save_pos = false;
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
    if (g->tilt_shake_pending) { /* a sharp movement: every control visible last frame wobbles once, staggered */
        g->tilt_shake_pending = false;
        ui_jiggle(&g->ui, g->tilt_shake_dir, app->time);
    }
    ui_begin(&g->ui, app->gfx, &app->font, &app->input);
    { /* TILT-1: this frame's tilt for the UI shaders (HUD at half strength in play); the safe rect bounds every shift */
        const float k = g->tilt_s * (g->playing ? k_ui.tilt_hud : 1.0f);
        const float safe[4] = {app->safe_left, app->safe_top, app->width_pt - app->safe_right, app->height_pt - app->safe_bottom};
        g->ui.tilt_x = g->tilt.tx * k, g->ui.tilt_y = g->tilt.ty * k;
        memcpy(g->ui.safe, safe, sizeof(safe));
        gfx_ui_set_tilt(app->gfx, g->ui.tilt_x, g->ui.tilt_y, k, safe);
        ui_jiggle_update(&g->ui);
    }
    {
        PROF_SCOPE("ui.anim");
        const bool sys_reduce = app_reduce_motion();
        if (sys_reduce != g->sys_reduce_motion || !g->sys_reduce_logged) { /* logged for the on-device check */
            g->sys_reduce_motion = sys_reduce, g->sys_reduce_logged = true;
            mg_log_info("ui: system Reduce Motion %s", sys_reduce ? "on" : "off");
        }
        ui_anim_begin(&g->ui_anim, app->time, s->ui_motion == 1 || sys_reduce || g->uitest.reduced || g->force_reduced);
    }
    g->ui_fx.sounds = s->sound_on && s->ui_sounds;
    { /* screen changes enter; the UI shake moves UI only, never the camera */
        const int key = !g->playing ? 100 + (int)g->fe.state : g->fpage.open ? 3 : g->menu.open && !g->menu.card ? 2 : 0;
        if (key != g->ui_screen_key) {
            g->ui_screen_key = key;
            g->ui_screen_t0 = app->time;
            tilt_snap(&g->tilt); /* TILT-1 A1: a screen opens at rest */
        }
        ui_screen_enter(&g->ui, (float)(app->time - g->ui_screen_t0), ui_fx_shake_x(&g->ui_fx));
    }
    bool back = app->input.key_pressed[SDL_SCANCODE_AC_BACK] || app->input.key_pressed[SDL_SCANCODE_ESCAPE];
    if (!g->playing) {
        frontend_flow(g, app, back);
        return;
    }
    const bool sp_on = script_player_active(&g->sp); /* BOOK-ENGINE: the script card is a modal over everything in play */
    if (back && sp_on) {
        script_player_close(&g->sp);
        progress_commit(g, true);
        back = false;
    }
    g->ui.input_off = g->ui.input_off || sp_on; /* what is under the card takes no touches */
    if (back && g->mui.journal) { /* the journal goes back to the pause card */
        g->mui.journal = false;
        back = false;
    } else if (back && missions_dialogue_open(&g->missions) && !g->menu.open) { /* a dialogue: later */
        missions_dialogue_close(&g->missions);
        back = false;
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
    if (g->menu.open != g->pause_was_open) { /* pause opens / the game resumes */
        g->pause_was_open = g->menu.open;
        ui_haptic(HAPTIC_PAUSE);
    }
    SettingsAccount acct = account_view(g);
    const bool stats_was = s->show_stats;
    MenuAction action = MENU_NONE;
    missions_frame(g, app); /* BOOK-ENGINE S4: the objective and its marker */
    if (g->fpage.open) { /* opened from the pause card; its back arrow returns there */
        friends_page_draw(&g->fpage, &g->ui, &g->friends, &g->lobby, &g->heads, app, g->profile.display_name, &g->profile.skin);
    } else if (g->mui.journal && g->menu.open) { /* the missions' journal, a page of the pause card */
        missions_overlays(g, app);
    } else {
        g->mui.journal = false;
        action = settings_menu(&g->menu, &g->ui, s, &acct, app->width_pt, app->height_pt, app->safe_left, app->safe_top, app->safe_right,
                               app->safe_bottom, g->status);
        missions_overlays(g, app); /* the script player and the dialogue card (not under the pause card) */
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
        avatar_animate(&g->avatar_meshes, &g->preview_anim, &pv, mg_minf(app->frame_dt, 0.1f));
        AvatarLook look = avatar_look_from_skin(g->profile.skin);
        avatar_draw(app->gfx, &g->avatar_meshes, &look, &pv, &g->preview_anim, NULL);
    }
    /* the panel works the lift the player stands in (kept while it slides out); none in: the nearest (forced panel) */
    const int lift_here = lift_containing(g, g->player.kin.pos, g->kparams.radius);
    if (lift_here >= 0) {
        g->panel_lift = lift_here;
    } else if (g->panel_lift < 0 || g->panel_lift >= lift_count(g)) {
        g->panel_lift = lift_nearest(g, g->player.kin.pos);
    }
    Elevator *el = lift_get(g, g->panel_lift);
    const bool riding = player_lift_busy(g); /* only the player's own car locks the input (several lifts: others run on calls) */
    /* auto-nav rides on its own: no panel unless the player cancels */
    const bool menu_shown = el && !riding && !g->menu.open && !g->no_panels && (!autonav_active(&g->autonav) || s->force_lift_panel) &&
                            (lift_here >= 0 || s->force_lift_panel);
    if (g->lift_panel_ok && el) {
        const ui_frame_t lf = hud_frame(app);
        if (menu_shown) {
            elevator_panel_set_floors(&g->lift_panel, el->floors); /* one button per landing of this lift */
        }
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
    if (g->menu.open || riding || book_modal(g) || sp_on) {
        touch_controls_reset(&g->touch);
        stick_view_update(&g->stick_view, &g->touch, &g->touch_cfg, &g->ui_fx, app->time, g->menu.open || book_modal(g) || sp_on, g->ui_anim.reduced); /* fades out under a menu */
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
            /* the car the path rides (nav car i = scene lift i), else the one at hand */
            Elevator *ael = g->autonav.path.ride_car >= 0 ? lift_get(g, g->autonav.path.ride_car) : el;
            AutonavCtx ac = {feet, scene_indoors(g->scene, feet) ? scene_floor_of(g->scene, feet) : -1, app->time,
                             ael && elevator_contains(ael, g->player.kin.pos, g->kparams.radius),
                             ael && elevator_busy(ael), ael ? ael->car_floor : 0};
            AutonavOut ao;
            PROF_SCOPE("autonav");
            autonav_update(&g->autonav, g->nav, &ac, &ao);
            g->auto_x = ao.move_x;
            g->auto_z = ao.move_z;
            if (ao.ride_to >= 0 && ael) {
                elevator_request(ael, ao.ride_to);
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
    if (scene_kind(g->scene) == SCENE_INSTITUTE) {
        institute_sky(&sp); /* the reference page's background (no fog); its lights live in GFX_MAT_PATTERN */
    }
#ifdef MG_RENDER_FULL
    sp.reflection_y = scene_floor_y(g->scene, floor);
#endif
#if !defined(MG_CAMERA_THIRDPERSON) && defined(MG_RENDER_FULL)
    sp.shadow_focus = g->fly_mode ? 0.0f : g->td.distance; /* cascades: 8 m / 30 m past the player */
#endif
    gfx_set_scene(app->gfx, &sp);

    /* M5 floor fade: inside, storeys above the view floor and the roof
     * dissolve over 350 ms (the third-person camera keeps the building whole; the fly cam
     * opens it only while the eye itself is inside) */
    bool inside = g->fly_mode ? scene_indoors(g->scene, sp.eye) : scene_indoors(g->scene, feet);
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
                      .storey_tint = s->show_storeys,
                      .inst_fill_1 = g->inst_fill_1};
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
    if (g->gallery.mode != GALLERY_OFF) {
        avatar_gallery_draw(&g->gallery, app->gfx, &g->avatar_meshes, mg_minf(app->frame_dt, 0.1f));
    }
    AvatarPose pose = {feet, player_render_yaw(&g->player, alpha), g->player.kin.vel, g->player.kin.grounded,
                       g->player.kin.jumping, scene_indoors(g->scene, feet), false};
    avatar_animate(&g->avatar_meshes, &g->local_anim, &pose, mg_minf(app->frame_dt, 0.1f));
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
        avatar_animate(&g->avatar_meshes, &r->anim, &rp, mg_minf(app->frame_dt, 0.1f));
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

    if (g->menu.open || book_modal(g)) {
        return;
    }
    if (g->nav) {
        markers_hud(g, app, floor, feet);
    }
    {
        const ui_frame_t cf = hud_frame(app);
        float cpx, cpy, cps;
        ui_pause_rect(&cf, &cpx, &cpy, &cps);
        const float chip_l = ui_safe_left(&cf) + UI_MARGIN + cf.corner; /* head_chip_draw takes the right edge */
        gfx_ui_depth(app->gfx, k_ui.tilt_depth[2], 0); /* TILT-1: the account pill */
        head_chip_draw(&g->heads, app->gfx, &app->font, &g->profile.skin, g->profile.display_name, chip_l + head_chip_width(&app->font, g->profile.display_name), cpy);
        gfx_ui_depth(app->gfx, k_ui.tilt_depth[1], 0);
    }
    g->touch.hide_hint = g->lift_panel_ok && g->lift_panel.slide > 0.0f;
    {
        float rx, ry;
        stick_view_rest(&g->touch_cfg, app->safe_left, app->height_pt, app->safe_bottom, &rx, &ry);
        stick_view_update(&g->stick_view, &g->touch, &g->touch_cfg, &g->ui_fx, app->time, g->menu.open || g->fpage.open || g->touch.hide_hint,
                          g->ui_anim.reduced);
        stick_view_draw(&g->stick_view, &g->touch, &g->touch_cfg, app->gfx, rx, ry);
    }
    touch_controls_draw(&g->touch, &g->touch_cfg, app->gfx, &app->font, 0.0f, app->height_pt, 0.0f); /* the jump button (off) */
    frontend_toast_draw(&g->fe, &g->ui, app);
    gfx_ui_depth(app->gfx, 0.0f, 0); /* TILT-1: world-anchored: name tags and chat clouds never shift */
    draw_name_tags(g, app, floor, player_floor, feet);
    gfx_ui_depth(app->gfx, k_ui.tilt_depth[1], 0);
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
    gfx_ui_depth(app->gfx, 0.0f, 0);
    draw_chat_bubbles(g, app, floor);
    gfx_ui_depth(app->gfx, k_ui.tilt_depth[1], 0);
    if (g->text_test) { /* --autotest text */
        const float tw = mg_minf(app->width_pt - 40.0f, 760.0f), tx = (app->width_pt - tw) * 0.5f, ty = app->height_pt * 0.5f - 70.0f;
        ui_round_rect(app->gfx, &app->font, tx, ty, tw, 140.0f, 14.0f, ui_rgba(0x101218F0));
        text_draw_centered(app->gfx, &app->font, tx + tw * 0.5f, ty + 44.0f, 30.0f, ui_rgba(0xFFFFFFFF), str("autotest_text"));
        text_draw_centered(app->gfx, &app->font, tx + tw * 0.5f, ty + 100.0f, 20.0f, ui_rgba(0xFFD166FF), str("autotest_text_line"));
        /* the book glyphs, and ~25 KB of placeholder text (invented syllables with every Romanian letter; no content
         * pack is needed) in a scrolling area that emits only its visible lines */
        if (!g->book_text) {
            static const char *const syl[] = {"ță", "râ", "șe", "lo", "mi", "ăn", "tî", "Ște", "Ța", "Ă", "Î", "bu", "ne", "„ce”", "–", "va", "zo"};
            const size_t cap = 26 * 1024;
            g->book_text = ARENA_PUSH(&app->perm, char, cap);
            g->book_lines = ARENA_PUSH(&app->perm, TextLine, 4096);
            ui_text_area_init(&g->book_area, g->book_lines, 4096);
            size_t n = 0;
            uint32_t r = 7;
            for (int w = 0; n + 64 < cap; w++) {
                r = r * 1103515245u + 12345u;
                const int k = 2 + (int)((r >> 16) % 3u);
                for (int j = 0; j < k && n + 64 < cap; j++) {
                    r = r * 1103515245u + 12345u;
                    n += (size_t)SDL_snprintf(g->book_text + n, cap - n, "%s", syl[(r >> 16) % MG_ARRAY_COUNT(syl)]);
                }
                n += (size_t)SDL_snprintf(g->book_text + n, cap - n, w % 97 == 96 ? ".\n\n" : " ");
            }
            mg_log_info("autotest text: a %u-byte placeholder text for the text area", (unsigned)n);
        }
        const float bt = 12.0f, bh = ty - 12.0f - bt;
        ui_round_rect(app->gfx, &app->font, tx, bt, tw, bh, 14.0f, ui_rgba(0x101218F0));
        text_draw_centered(app->gfx, &app->font, tx + tw * 0.5f, bt + 16.0f, 18.0f, ui_rgba(0xFFD166FF), str("autotest_text_book"));
        if (g->book_text) {
            uint32_t a0, a1;
            gfx_ui_instances(app->gfx, &a0);
            ui_text_area(&g->ui, &g->book_area, g->book_text, TEXT_FACE_BODY, 15.0f, ui_rgba(0xFFFFFFFF), tx + 16.0f, bt + 34.0f, tw - 32.0f,
                         mg_maxf(bh - 42.0f, 20.0f));
            gfx_ui_instances(app->gfx, &a1);
            g->book_area_inst = a1 - a0;
        }
        /* label centring: read back what three buttons emit; the capitals' middle against the face's middle */
        static const struct { float h; int flags; } btn[3] = {{54.0f, 0}, {54.0f, UI_F_SMALL}, {44.0f, UI_F_SMALL}};
        const TextGlyph *H = &app->font_display.glyphs['H'];
        g->text_center_err = 0.0f;
        for (int b = 0; b < 3; b++) {
            char id[24];
            SDL_snprintf(id, sizeof(id), "HHH##tc%d", b);
            uint32_t n0, n1;
            gfx_ui_instances(app->gfx, &n0);
            ui_button_v(&g->ui, id, tx + (float)b * 220.0f, ty + 160.0f, 200.0f, btn[b].h, UI_V_PRIMARY, btn[b].flags);
            const GfxUiInst *in = gfx_ui_instances(app->gfx, &n1);
            float face_c = -1.0f, cap_c = -1.0f;
            for (uint32_t i = n0; i < n1; i++) {
                if (in[i].kind == (float)GFX_UI_SHAPE && in[i].shape[2] > 0.0f) { /* the face: the shape with the highlight band */
                    face_c = in[i].rect[1] + in[i].rect[3] * 0.5f;
                } else if (in[i].kind == (float)GFX_UI_GLYPH && H->h > 0.0f) {
                    const float scale = in[i].rect[3] / H->h, baseline = in[i].rect[1] - H->yoff * scale;
                    cap_c = baseline - app->font_display.cap_px * scale * 0.5f;
                }
            }
            const float err = face_c < 0.0f || cap_c < 0.0f ? 99.0f : fabsf(cap_c - face_c);
            g->text_center_err = mg_maxf(g->text_center_err, err);
            g->text_center_each[b] = err;
        }
    }
    if (g->greet_t > 0.0f) { /* bottom right, beside the preview card */
        g->greet_t = mg_maxf(g->greet_t - mg_minf(app->frame_dt, 0.1f), 0.0f);
        float gr[4];
        greet_rect(g, app, gr);
        const float cw = gr[2], ch = gr[3], cx = gr[0], cy = gr[1];
        ui_surface(&g->ui, cx, cy, cw, ch - k_ui.lip_card, 16.0f, k_ui.lip_card, ui_rgba(k_ui.surface), ui_rgba(k_ui.lip_mint), 0.0f, ui_rgba(0));
        text_draw_centered(app->gfx, &app->font_display, cx + cw * 0.5f, cy + 18.0f, 14.0f, ui_rgba(k_ui.teal_text), g->greet_name);
        text_draw_centered(app->gfx, &app->font, cx + cw * 0.5f, cy + 40.0f, 15.0f, ui_rgba(k_ui.ink), g->greet_line);
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
    if (g->perf.active && SDL_strcmp(g->perf.scenario, "book") == 0 && !script_player_active(&g->sp) && g->book.ok && g->book.script_count > 0) {
        script_player_open(&g->sp, &g->book, 0, "perf"); /* perf "book": a card open all the time */
    }
    if (!missions_playing(&g->missions) && script_player_active(&g->sp)) { /* BOOK-ENGINE S5: a script opened on its own (book_ui, perf
                                                                            * book), last, on top; mission steps play in missions_overlays */
        g->ui.input_off = false;
        if (script_player_frame(&g->sp, &g->ui, app, &g->progress, wall_clock_ms())) {
            progress_commit(g, true); /* a script ended (finished or closed): the file now, the server when online */
        }
    }
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
    if (g->settings.ui_sounds) {
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
    const HMM_Vec3 goal = id == g->mission_marker ? mission_walk_pos(g, m->pos, m->storey) : m->pos;
    if (autonav_go(&g->autonav, g->nav, &c, goal, m->storey, id)) {
        g->active_marker = id;
        marker_set_state(m, MARKER_TAPPED);
        marker_cue(g, AUDIO_CUE_TICK);
        ui_haptic(HAPTIC_MARKER_TAP);
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
    ui_haptic(HAPTIC_MARKER_REACHED);
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
        if (!outdoor && scene_area_kind(g->scene, p.X, p.Z, storey) != want) {
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
        g->marker_override = count ? SDL_atoi(count) : (at && SDL_strcmp(at, "markers") == 0 ? 8 : (g->perf.active || (at && (SDL_strcmp(at, "interact") == 0 || SDL_strcmp(at, "missions") == 0 || SDL_strcmp(at, "book") == 0)) ? 0 : (at && SDL_strcmp(at, "hudclamp") == 0 ? 16 : -1)));
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
        ui_haptic(HAPTIC_NAV_CANCEL);
        if (a) {
            marker_set_state(a, MARKER_IDLE);
            marker_cue(g, AUDIO_CUE_DUP);
        }
        g->active_marker = 0;
        autonav_clear(&g->autonav);
    } else if (g->autonav.state == AUTONAV_FAILED) {
        ui_haptic(HAPTIC_REFUSED); /* no path */
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
                            scale, shake, m->ring_t, tint, {0}};
        floor_badge(g, in->storey, in->badge);
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
        const float chip[4] = {ui_safe_left(&uf) + UI_MARGIN + uf.corner, py, CHIP_RESERVED_W, CHIP_H};
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
    if (g->mui.pill[2] > 0.0f) { /* BOOK-ENGINE: the objective pill */
        memcpy(res + rc * 4, g->mui.pill, sizeof(g->mui.pill));
        rc++;
    }
    g->hud_res_count = rc;
    const ui_frame_t mf = hud_frame(app);
    MarkerHudView v = {g->view_proj, w, h, mf.inset_l, mf.inset_t, mf.inset_r, mf.inset_b, view_floor, res, rc};
    g->hud_count = marker_hud_layout(g->hud_in, n, &v, g->hud_out);
    gfx_ui_depth(app->gfx, k_ui.tilt_depth[3], 0); /* TILT-1: marker edge icons */
    marker_hud_draw(app->gfx, &app->font, g->hud_out, g->hud_count, view_floor);
    gfx_ui_depth(app->gfx, k_ui.tilt_depth[1], 0);
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
    const float r = g->touch_cfg.radius_pt, lip = k_ui.stick_lip_off * r / k_ui.stick_design_r;
    float stick_cx, stick_cy;
    stick_view_rest(&g->touch_cfg, app->safe_left, app->height_pt, app->safe_bottom, &stick_cx, &stick_cy);
    const float stick_x = stick_cx - r, stick_y = stick_cy - r;
    const float nk = f.h >= 600.0f ? 1.3f : 1.0f; /* the join panel, ui/notice_panel.c */
    const struct { const char *name; float x, y, w, h; } items[] = {
        {"pause", px, py, ps, ps},
        {"stick", stick_x, stick_y, 2.0f * r, 2.0f * r + lip},
        {"chip", ui_safe_left(&f) + UI_MARGIN + f.corner, py, CHIP_RESERVED_W, CHIP_H},
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
    /* the stick (UI-1b) keeps to the device's real safe rect, so an edge may be used from its real inset */
    const float real[4] = {app->safe_left, app->safe_top, app->safe_right, app->safe_bottom};
    for (int e = 0; e < 4; e++) {
        const bool close = inset[e] > 0.0f || gap[e] <= 20.0f + f.corner + mg_maxf(real[e], 0.0f);
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
    const float head = avatar_name_tag_y(&g->avatar_meshes, NULL, 0.0f);
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
    ui_haptics_active(false); /* the engine stops; nothing plays in the background */
    motion_stop(); /* tilt: the sensor stops; tilt_step restarts it and snaps the neutral pose */
    g->motion_on = false;
}

void game_resume(MgApp *app)
{
    Game *g = (Game *)app->game;
    net_set_background(g->net, false);
    lobby_foreground(&g->lobby);
    ui_haptics_active(true);
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
