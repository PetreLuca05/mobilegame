/* Game: scene (hospital or dev room), kinematic player, landscape touch
 * controls, third-person camera, HUD, settings menu and multiplayer session.
 * Keyboard/mouse also work on desktop. */
#include "game.h"

#include "autotest.h"
#include "avatar.h"
#include "camera_tp.h"
#include "config.local.h"
#include "debug_menu.h"
#include "launch_screen.h"
#include "gfx/camera.h"
#include "gfx/material.h"
#include "gfx/mesh.h"
#include "net_session.h"
#include "player.h"
#include "scene.h"
#include "scenery.h"
#include "settings.h"
#include "ui/hud.h"
#include "ui/touch_controls.h"

typedef struct Game {
    Scene *scene;
    ColWorld world;
    KinematicParams kparams;
    Player player;
    TpCamera cam;
    FlyCamera fly;
    bool fly_mode;
    GameSettings settings;
    TouchControlsConfig touch_cfg;
    TouchControls touch;
    Ui ui;
    DebugMenu menu;
    GfxMeshId unit_box;
    GfxMeshId ref_capsule; /* 1.8 m x 0.35 m player-sized ghost for the collider view */
    Scenery scenery;
    AvatarMeshes avatar_meshes;
    AvatarAnim local_anim;
    AvatarLook local_look;
    NetTransport *net;
    NetSession session;
    bool demo_walk;
    LaunchScreen launch;
    char status[96];
    Autotest autotest;
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

bool game_init(MgApp *app)
{
    Game *g = ARENA_PUSH(&app->perm, Game, 1);
    app->game = g;
    g->scene = scene_create(app->gfx, &app->perm, &g->world, &app->font);
    if (!g->scene) {
        return false;
    }
    kinematic_default_params(&g->kparams);
    settings_default(&g->settings);
    touch_controls_default_config(&g->touch_cfg);
    if (!PLAYER_JUMP_ENABLED) {
        g->touch_cfg.jump_size_pt = 0.0f; /* no jump button */
    }
    touch_controls_reset(&g->touch);

    HMM_Vec3 spawn = scene_spawn(g->scene, 0);
    parse_vec3_arg(app->argc, app->argv, "--spawn", &spawn);
    player_init(&g->player, spawn, scene_spawn_yaw(g->scene));
    tpcam_init(&g->cam);
    g->fly.pos = HMM_V3(0.0f, 8.0f, 11.5f);
    g->fly.pitch = HMM_AngleDeg(-32.0f);
    g->fly_mode = parse_cam_arg(app->argc, app->argv, &g->fly);
    g->settings.show_colliders = has_flag(app->argc, app->argv, "--colliders");
    g->menu.open = has_flag(app->argc, app->argv, "--menu");

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
    SceneryShape shape = scene_scenery_shape();
    if (!scenery_init(&g->scenery, &shape, app->gfx, &app->perm) ||
        !avatar_meshes_init(&g->avatar_meshes, app->gfx, &app->perm)) {
        return false;
    }
    g->local_look = avatar_look_doctor(avatar_look_for_slot(0));

    const char *profile = arg_value(app->argc, app->argv, "--profile");
    NetConfig ncfg = {MG_SUPABASE_HOST, MG_SUPABASE_ANON_KEY, profile ? profile : "", 25, 3,
                      has_flag(app->argc, app->argv, "--public-test")};
    g->net = net_create(&app->perm, &ncfg);
    session_init(&g->session, g->net);
    const char *party = arg_value(app->argc, app->argv, "--party");
    if (party) {
        session_start(&g->session, MODE_PARTY, party);
    } else if (has_flag(app->argc, app->argv, "--world")) {
        session_start(&g->session, MODE_WORLD, NULL);
    }
    /* the launch screen comes first unless a test flag already chose */
    g->launch.active = !party && !has_flag(app->argc, app->argv, "--world") &&
                       !has_flag(app->argc, app->argv, "--offline") && !has_flag(app->argc, app->argv, "--autotest") &&
                       !arg_value(app->argc, app->argv, "--spawn") && !arg_value(app->argc, app->argv, "--cam") &&
                       !has_flag(app->argc, app->argv, "--menu") && !has_flag(app->argc, app->argv, "--colliders");
    SDL_snprintf(g->status, sizeof(g->status), "offline");
    autotest_init(&g->autotest, app->argc, app->argv);
    g->demo_walk = has_flag(app->argc, app->argv, "--demo-walk");
    return g->unit_box != GFX_MESH_NONE;
}

void game_tick(MgApp *app)
{
    Game *g = (Game *)app->game;
    scene_tick(g->scene, &g->world, MG_TICK_DT);
    player_tick(&g->player, &g->kparams, &g->world, MG_TICK_DT);
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
    float look_y = g->touch.camera_dy + in->mouse_dy;
    look_x += ((k[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f)) * 400.0f * dt;
    look_y += ((k[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_UP] ? 1.0f : 0.0f)) * 300.0f * dt;
    float invert = s->invert_y ? -1.0f : 1.0f;
    tpcam_orbit(&g->cam, -HMM_AngleDeg(s->yaw_deg_per_pt) * look_x, -HMM_AngleDeg(s->pitch_deg_per_pt) * look_y * invert);
    g->settings.cam_pitch_deg = HMM_ToDeg(g->cam.pitch);

    float fw = g->touch.move_y + (k[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_S] ? 1.0f : 0.0f);
    float st = g->touch.move_x + (k[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_A] ? 1.0f : 0.0f);
    float sy = HMM_SinF(g->cam.yaw), cy = HMM_CosF(g->cam.yaw);
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

static void handle_menu_action(Game *g, MenuAction a)
{
    switch (a) {
    case MENU_JOIN:
        session_start(&g->session, MODE_PARTY, g->settings.room_code);
        break;
    case MENU_NEW_ROOM:
        session_start(&g->session, MODE_PARTY, NULL);
        SDL_snprintf(g->settings.room_code, sizeof(g->settings.room_code), "%s", g->session.room_code);
        break;
    case MENU_DISCONNECT:
        session_say_goodbye(&g->session, 0, g->player.kin.pos, g->player.yaw);
        session_stop(&g->session);
        break;
    case MENU_CAMERA_PITCH:
        g->cam.pitch = HMM_AngleDeg(g->settings.cam_pitch_deg);
        break;
    case MENU_MAIN_MENU:
        session_say_goodbye(&g->session, 0, g->player.kin.pos, g->player.yaw);
        session_stop(&g->session);
        g->launch.active = true;
        g->launch.page = 0;
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

static void scene_defaults(GfxSceneParams *sp, float time)
{
    sp->sun_dir = HMM_NormV3(HMM_V3(0.5f, 0.75f, 0.45f));
    sp->sun_color = HMM_V3(0.48f, 0.46f, 0.42f);
    sp->ambient = HMM_V3(0.58f, 0.6f, 0.68f);
    sp->sky_horizon = mg_rgb(0xBFE3FF);
    sp->sky_zenith = mg_rgb(0x3F8FE8);
    sp->reflection_y = 0.0f;
    sp->reflection_strength = 1.0f;
    sp->time = time;
}

/* Launch screen: slow orbit around the scene, menu on top. */
static void game_frame_launch(Game *g, MgApp *app)
{
    GfxSceneParams sp = {0};
    HMM_Vec3 target;
    scene_launch_view(g->scene, (float)app->time, &sp.eye, &target);
    sp.view = HMM_LookAt_RH(sp.eye, target, HMM_V3(0, 1, 0));
    sp.proj = camera_projection(HMM_AngleDeg(62.0f), app->width_pt / app->height_pt, 0.05f, 400.0f, -0.25f);
    scene_defaults(&sp, (float)app->time);
    gfx_set_scene(app->gfx, &sp);
    SceneView view = {sp.eye, 0, (float)app->time, false};
    scene_draw(g->scene, app->gfx, &view);
    scenery_draw(&g->scenery, app->gfx, &sp, (float)app->time);
    for (int i = 0; i < SCENE_SPAWNS; i++) { /* the cast, idling on their discs */
        AvatarLook look = i == 0 ? avatar_look_doctor(avatar_look_for_slot(0)) : avatar_look_for_slot(i);
        AvatarPose pose = {scene_spawn(g->scene, i), scene_spawn_yaw(g->scene) + HMM_PI32 + 0.3f * (float)(i - 1),
                           HMM_V3(0, 0, 0), true, false};
        avatar_draw(app->gfx, &g->avatar_meshes, &look, &pose, &g->local_anim, &g->world);
    }
    avatar_animate(&g->local_anim, &(AvatarPose){HMM_V3(0, 0, 0), 0, HMM_V3(0, 0, 0), true, false},
                   mg_minf(app->frame_dt, 0.1f));

    LaunchChoice c = launch_screen(&g->launch, &g->ui, app->width_pt, app->height_pt, app->safe_left,
                                   app->safe_top, app->safe_right, app->safe_bottom, g->net != NULL);
    switch (c) {
    case LAUNCH_OFFLINE:
        session_stop(&g->session);
        player_init(&g->player, scene_spawn(g->scene, 0), scene_spawn_yaw(g->scene));
        break;
    case LAUNCH_CREATE_PARTY:
        session_start(&g->session, MODE_PARTY, NULL);
        SDL_snprintf(g->settings.room_code, sizeof(g->settings.room_code), "%s", g->session.room_code);
        break;
    case LAUNCH_JOIN_PARTY:
        session_start(&g->session, MODE_PARTY, g->launch.code);
        SDL_snprintf(g->settings.room_code, sizeof(g->settings.room_code), "%s", g->launch.code);
        break;
    case LAUNCH_WORLD:
        session_start(&g->session, MODE_WORLD, NULL);
        break;
    default:
        break;
    }
    if (c != LAUNCH_NONE) {
        g->cam.initialized = false;
        touch_controls_reset(&g->touch);
    }
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

void game_frame(MgApp *app, float alpha)
{
    Game *g = (Game *)app->game;
    GameSettings *s = &g->settings;
    AutotestProbe probe = {g->player.kin.pos, g->player.kin.vel, HMM_ToDeg(g->cam.yaw), g->player.kin.jump_count,
                           g->touch.sprint, 1};
    autotest_update(&g->autotest, app, &probe);

    /* network: events, remote buffers, our state at the adaptive rate */
    {
        const KinematicState *k = &g->player.kin;
        uint8_t flags = (uint8_t)((k->grounded ? REP_GROUNDED : 0) | (k->jumping ? REP_JUMPING : 0) |
                                  (g->player.sprint ? REP_SPRINTING : 0));
        session_update(&g->session, app->time, app->clock.tick, k->pos, k->vel, g->player.yaw, flags);
        if (g->session.spawn_pending) {
            g->session.spawn_pending = false;
            int slot = g->session.slot < 0 ? 0 : g->session.slot;
            player_init(&g->player, scene_spawn(g->scene, slot), scene_spawn_yaw(g->scene));
            g->cam.initialized = false;
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
    if (g->launch.active) {
        game_frame_launch(g, app);
        return;
    }
    MenuAction action = debug_menu(&g->menu, &g->ui, s, app->width_pt, app->height_pt, app->safe_left,
                                   app->safe_top, app->safe_right, app->safe_bottom, g->status);
    handle_menu_action(g, action);
    g->touch_cfg.dead_zone_pt = s->dead_zone_pt;
    gfx_set_render_scale(app->gfx, s->render_scale);
    g->cam.distance = s->cam_distance;
    g->cam.fov_y = HMM_AngleDeg(s->fov_deg);
    if (g->menu.open) {
        touch_controls_reset(&g->touch);
    } else {
        touch_controls_update(&g->touch, &g->touch_cfg, &app->input, app->width_pt, app->height_pt, app->safe_right,
                              app->safe_bottom);
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
        update_player_input(g, app);
        tpcam_update(&g->cam, feet, mg_minf(app->frame_dt, 0.1f));
        tpcam_matrices(&g->cam, aspect, &sp.view, &sp.proj, &sp.eye);
    }
    int floor = scene_floor_of(g->scene, g->fly_mode ? HMM_SubV3(sp.eye, HMM_V3(0, 1.5f, 0)) : feet);
    scene_defaults(&sp, (float)app->time);
    sp.reflection_y = scene_floor_y(g->scene, floor);
    gfx_set_scene(app->gfx, &sp);

    SceneView view = {sp.eye, floor, (float)app->time, s->show_colliders};
    scene_draw(g->scene, app->gfx, &view);
    scenery_draw(&g->scenery, app->gfx, &sp, (float)app->time);
    AvatarPose pose = {feet, player_render_yaw(&g->player, alpha), g->player.kin.vel, g->player.kin.grounded,
                       g->player.kin.jumping};
    avatar_animate(&g->local_anim, &pose, mg_minf(app->frame_dt, 0.1f));
    avatar_draw(app->gfx, &g->avatar_meshes, &g->local_look, &pose, &g->local_anim, &g->world);
    for (int i = 0; i < SESSION_MAX_REMOTE; i++) {
        RemotePlayer *r = &g->session.remotes[i];
        if (!r->active || !r->visible) {
            continue;
        }
        AvatarPose rp = {r->pos, r->yaw, r->sample.vel, (r->sample.flags & REP_GROUNDED) != 0,
                         (r->sample.flags & REP_JUMPING) != 0};
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
    draw_room_badge(g, app);
}

void game_event(MgApp *app, const SDL_Event *e)
{
    (void)app;
    (void)e;
}

void game_pause(MgApp *app)
{
    Game *g = (Game *)app->game;
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
    if (g && g->net) {
        session_say_goodbye(&g->session, app->clock.tick, g->player.kin.pos, g->player.yaw);
        net_destroy(g->net); /* sends phx_leave before closing */
        g->net = NULL;
    }
}
