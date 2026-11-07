/* Step 2: dev room with a free-fly debug camera. */
#include "game.h"

#include "devroom.h"
#include "gfx/camera.h"
#include "gfx/material.h"
#include "gfx/mesh.h"

typedef struct Game {
    DevRoom room;
    FlyCamera fly;
    int move_touch; /* touch slot steering the fly camera, -1 none */
    int look_touch;
} Game;

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
    g->fly.pos = HMM_V3(0.0f, 8.0f, 11.5f);
    g->fly.yaw = 0.0f;
    g->fly.pitch = HMM_AngleDeg(-32.0f);
    parse_cam_arg(app->argc, app->argv, &g->fly);
    g->move_touch = -1;
    g->look_touch = -1;
    return true;
}

void game_tick(MgApp *app) { (void)app; }

/* Free-fly debug camera: left-half drag moves, right-half drag looks;
 * WASD + Q/E + arrows on desktop, right mouse drag looks. */
static void update_fly_camera(Game *g, MgApp *app)
{
    MgInput *in = &app->input;
    float dt = mg_minf(app->frame_dt, 0.1f);
    float fwd = 0.0f, strafe = 0.0f, up = 0.0f;
    float look_x = 0.0f, look_y = 0.0f;
    for (int i = 0; i < MG_MAX_TOUCHES; i++) {
        MgTouch *t = &in->touches[i];
        if (t->began && t->owner == MG_TOUCH_FREE) {
            if (t->x0 < app->width_pt * 0.5f && g->move_touch < 0) {
                t->owner = 1;
                g->move_touch = i;
            } else if (g->look_touch < 0) {
                t->owner = 2;
                g->look_touch = i;
            }
        }
    }
    if (g->move_touch >= 0) {
        MgTouch *t = &in->touches[g->move_touch];
        fwd = mg_clampf(-(t->y - t->y0) / 80.0f, -1.0f, 1.0f);
        strafe = mg_clampf((t->x - t->x0) / 80.0f, -1.0f, 1.0f);
        if (!t->active) {
            g->move_touch = -1;
        }
    }
    if (g->look_touch >= 0) {
        MgTouch *t = &in->touches[g->look_touch];
        look_x += t->dx;
        look_y += t->dy;
        if (!t->active) {
            g->look_touch = -1;
        }
    }
    const bool *k = in->key_down;
    fwd += (k[SDL_SCANCODE_W] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_S] ? 1.0f : 0.0f);
    strafe += (k[SDL_SCANCODE_D] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_A] ? 1.0f : 0.0f);
    up += (k[SDL_SCANCODE_E] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_Q] ? 1.0f : 0.0f);
    look_x += in->mouse_dx + ((k[SDL_SCANCODE_RIGHT] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_LEFT] ? 1.0f : 0.0f)) * 240.0f * dt;
    look_y += in->mouse_dy + ((k[SDL_SCANCODE_DOWN] ? 1.0f : 0.0f) - (k[SDL_SCANCODE_UP] ? 1.0f : 0.0f)) * 240.0f * dt;

    g->fly.yaw -= HMM_AngleDeg(0.25f) * look_x;
    g->fly.pitch = mg_clampf(g->fly.pitch - HMM_AngleDeg(0.2f) * look_y, HMM_AngleDeg(-89.0f), HMM_AngleDeg(89.0f));
    HMM_Vec3 f = camera_forward(g->fly.yaw, g->fly.pitch);
    HMM_Vec3 r = HMM_NormV3(HMM_Cross(f, HMM_V3(0, 1, 0)));
    const float speed = 6.0f;
    HMM_Vec3 v = HMM_AddV3(HMM_AddV3(HMM_MulV3F(f, fwd), HMM_MulV3F(r, strafe)), HMM_V3(0, up, 0));
    g->fly.pos = HMM_AddV3(g->fly.pos, HMM_MulV3F(v, speed * dt));
}

static void draw_hud(MgApp *app)
{
    char buf[96];
    Game *g = (Game *)app->game;
    float x = app->safe_left + 10.0f;
    float y = app->safe_top + 8.0f;
    SDL_snprintf(buf, sizeof(buf), "%.0f fps  %.1f ms", app->fps, app->frame_ms);
    text_draw_shadow(app->gfx, &app->font, x, y, 16.0f, ui_rgba(0xFFFFFFFF), buf);
    SDL_snprintf(buf, sizeof(buf), "cam %.1f %.1f %.1f", g->fly.pos.X, g->fly.pos.Y, g->fly.pos.Z);
    text_draw_shadow(app->gfx, &app->font, x, y + 20.0f, 14.0f, ui_rgba(0xFFFFFFE0), buf);
}

void game_frame(MgApp *app, float alpha)
{
    (void)alpha;
    Game *g = (Game *)app->game;
    update_fly_camera(g, app);

    GfxSceneParams sp = {0};
    sp.eye = g->fly.pos;
    sp.view = camera_view(g->fly.pos, g->fly.yaw, g->fly.pitch);
    sp.proj = camera_projection(HMM_AngleDeg(70.0f), app->width_pt / app->height_pt, 0.05f, 400.0f, 0.0f);
    sp.sun_dir = HMM_NormV3(HMM_V3(0.45f, 0.8f, 0.35f));
    sp.sun_color = HMM_V3(0.48f, 0.46f, 0.42f);
    sp.ambient = HMM_V3(0.58f, 0.6f, 0.68f);
    sp.sky_horizon = mg_rgb(0xBFE3FF);
    sp.sky_zenith = mg_rgb(0x3F8FE8);
    sp.time = (float)app->time;
    gfx_set_scene(app->gfx, &sp);

    devroom_draw(&g->room, app->gfx, true);
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
