/* Step 3: kinematic player in the dev room, keyboard controls on desktop. */
#include "game.h"

#include "camera_tp.h"
#include "devroom.h"
#include "gfx/camera.h"
#include "gfx/material.h"
#include "gfx/mesh.h"
#include "player.h"

typedef struct Game {
    DevRoom room;
    ColWorld world;
    KinematicParams kparams;
    Player player;
    TpCamera cam;
    FlyCamera fly;
    bool fly_mode;
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
    if (!devroom_init(&g->room, app->gfx, &app->perm)) {
        return false;
    }
    devroom_colliders(&g->room, &g->world);
    kinematic_default_params(&g->kparams);
    HMM_Vec3 spawn = g->room.spawns[0];
    parse_vec3_arg(app->argc, app->argv, "--spawn", &spawn);
    player_init(&g->player, spawn, 0.0f);
    tpcam_init(&g->cam);
    g->fly.pos = HMM_V3(0.0f, 8.0f, 11.5f);
    g->fly.pitch = HMM_AngleDeg(-32.0f);
    g->fly_mode = parse_cam_arg(app->argc, app->argv, &g->fly);
    return true;
}

void game_tick(MgApp *app)
{
    Game *g = (Game *)app->game;
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
    float look_x = in->mouse_dx + ((k[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f)) * 240.0f * dt;
    float look_y = in->mouse_dy + ((k[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_UP] ? 1.0f : 0.0f)) * 240.0f * dt;
    for (int i = 0; i < MG_MAX_TOUCHES; i++) {
        MgTouch *t = &in->touches[i];
        if (!t->active) {
            continue;
        }
        if (t->x0 < app->width_pt * 0.5f) {
            fwd += mg_clampf(-(t->y - t->y0) / 80.0f, -1.0f, 1.0f);
            strafe += mg_clampf((t->x - t->x0) / 80.0f, -1.0f, 1.0f);
        } else {
            look_x += t->dx;
            look_y += t->dy;
        }
    }
    g->fly.yaw -= HMM_AngleDeg(0.25f) * look_x;
    g->fly.pitch = mg_clampf(g->fly.pitch - HMM_AngleDeg(0.2f) * look_y, HMM_AngleDeg(-89.0f), HMM_AngleDeg(89.0f));
    HMM_Vec3 f = camera_forward(g->fly.yaw, g->fly.pitch);
    HMM_Vec3 r = HMM_NormV3(HMM_Cross(f, HMM_V3(0, 1, 0)));
    HMM_Vec3 v = HMM_AddV3(HMM_AddV3(HMM_MulV3F(f, fwd), HMM_MulV3F(r, strafe)), HMM_V3(0, up, 0));
    g->fly.pos = HMM_AddV3(g->fly.pos, HMM_MulV3F(v, 6.0f * dt));
}

/* Keyboard: WASD relative to the camera, Shift sprint, Space jump,
 * arrows / right mouse drag orbit the camera. */
static void update_player_input(Game *g, MgApp *app)
{
    MgInput *in = &app->input;
    const bool *k = in->key_down;
    float dt = mg_minf(app->frame_dt, 0.1f);
    float fw = (k[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_S] ? 1.0f : 0.0f);
    float st = (k[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_A] ? 1.0f : 0.0f);
    float dyaw = in->mouse_dx + ((k[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f)) * 400.0f * dt;
    float dpitch = in->mouse_dy + ((k[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_UP] ? 1.0f : 0.0f)) * 300.0f * dt;
    tpcam_orbit(&g->cam, -HMM_AngleDeg(0.25f) * dyaw, -HMM_AngleDeg(0.2f) * dpitch);

    float sy = HMM_SinF(g->cam.yaw), cy = HMM_CosF(g->cam.yaw);
    float mx = -sy * fw + cy * st;
    float mz = -cy * fw - sy * st;
    float len = HMM_SqrtF(mx * mx + mz * mz);
    if (len > 1.0f) {
        mx /= len;
        mz /= len;
    }
    bool sprint = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
    player_set_input(&g->player, mx, mz, sprint, k[SDL_SCANCODE_SPACE]);
}

static void draw_hud(MgApp *app)
{
    Game *g = (Game *)app->game;
    char buf[96];
    float x = app->safe_left + 10.0f;
    float y = app->safe_top + 8.0f;
    UiColor white = ui_rgba(0xFFFFFFFF);
    SDL_snprintf(buf, sizeof(buf), "%.0f fps  %.1f ms", app->fps, app->frame_ms);
    text_draw_shadow(app->gfx, &app->font, x, y, 16.0f, white, buf);
    const KinematicState *k = &g->player.kin;
    SDL_snprintf(buf, sizeof(buf), "pos %.2f %.2f %.2f  %s", k->pos.X, k->pos.Y, k->pos.Z,
                 k->grounded ? "ground" : "air");
    text_draw_shadow(app->gfx, &app->font, x, y + 20.0f, 14.0f, white, buf);
    float hs = HMM_SqrtF(k->vel.X * k->vel.X + k->vel.Z * k->vel.Z);
    SDL_snprintf(buf, sizeof(buf), "speed %.2f m/s  vy %.2f", hs, k->vel.Y);
    text_draw_shadow(app->gfx, &app->font, x, y + 38.0f, 14.0f, white, buf);
}

void game_frame(MgApp *app, float alpha)
{
    Game *g = (Game *)app->game;
    if (app->input.key_pressed[SDL_SCANCODE_F1]) {
        g->fly_mode = !g->fly_mode;
    }
    HMM_Vec3 feet = player_render_pos(&g->player, alpha);
    GfxSceneParams sp = {0};
    float aspect = app->width_pt / app->height_pt;
    if (g->fly_mode) {
        player_set_input(&g->player, 0, 0, false, false);
        update_fly_camera(g, app);
        sp.eye = g->fly.pos;
        sp.view = camera_view(g->fly.pos, g->fly.yaw, g->fly.pitch);
        sp.proj = camera_projection(HMM_AngleDeg(70.0f), aspect, 0.05f, 400.0f, 0.0f);
    } else {
        update_player_input(g, app);
        tpcam_update(&g->cam, feet, mg_minf(app->frame_dt, 0.1f));
        tpcam_matrices(&g->cam, aspect, &sp.view, &sp.proj, &sp.eye);
    }
    sp.sun_dir = HMM_NormV3(HMM_V3(0.45f, 0.8f, 0.35f));
    sp.sun_color = HMM_V3(0.48f, 0.46f, 0.42f);
    sp.ambient = HMM_V3(0.58f, 0.6f, 0.68f);
    sp.sky_horizon = mg_rgb(0xBFE3FF);
    sp.sky_zenith = mg_rgb(0x3F8FE8);
    sp.time = (float)app->time;
    gfx_set_scene(app->gfx, &sp);

    devroom_draw(&g->room, app->gfx, false);
    HMM_Mat4 m = HMM_MulM4(HMM_Translate(feet), HMM_Rotate_RH(player_render_yaw(&g->player, alpha), HMM_V3(0, 1, 0)));
    GfxMaterial cap = mat_toon(mg_rgb(0xF26B3A), 0.35f);
    gfx_draw(app->gfx, g->room.ref_capsule, &m, &cap, GFX_DRAW_REFLECT);
    draw_hud(app);
}

void game_event(MgApp *app, const SDL_Event *e)
{
    (void)app;
    (void)e;
}

void game_pause(MgApp *app) { (void)app; }
void game_resume(MgApp *app) { (void)app; }
void game_quit(MgApp *app) { (void)app; }
