/* Step 1: one triangle over the sky gradient, fps counter in the corner. */
#include "game.h"

#include "gfx/material.h"
#include "gfx/mesh.h"

typedef struct Game {
    GfxMeshId triangle;
    float spin;
} Game;

bool game_init(MgApp *app)
{
    Game *g = ARENA_PUSH(&app->perm, Game, 1);
    app->game = g;
    size_t mark = arena_mark(&app->perm);
    MeshBuilder mb;
    mb_init(&mb, &app->perm, 16, 16);
    HMM_Vec3 n = HMM_V3(0, 0, 1);
    uint32_t a = mb_vertex(&mb, HMM_V3(-0.6f, -0.4f, 0), n, HMM_V4(1.0f, 0.35f, 0.3f, 1));
    uint32_t b = mb_vertex(&mb, HMM_V3(0.6f, -0.4f, 0), n, HMM_V4(0.3f, 0.85f, 0.4f, 1));
    uint32_t c = mb_vertex(&mb, HMM_V3(0.0f, 0.6f, 0), n, HMM_V4(0.3f, 0.5f, 1.0f, 1));
    mb_triangle_idx(&mb, a, b, c);
    g->triangle = mb_upload(&mb, app->gfx);
    arena_reset_to(&app->perm, mark);
    return g->triangle != GFX_MESH_NONE;
}

void game_tick(MgApp *app)
{
    Game *g = (Game *)app->game;
    g->spin += MG_TICK_DT * 0.8f;
}

void game_frame(MgApp *app, float alpha)
{
    Game *g = (Game *)app->game;
    float spin = g->spin + alpha * MG_TICK_DT * 0.8f;
    float aspect = app->width_pt / app->height_pt;

    GfxSceneParams sp = {0};
    sp.eye = HMM_V3(0, 0, 2.5f);
    sp.view = HMM_LookAt_RH(sp.eye, HMM_V3(0, 0, 0), HMM_V3(0, 1, 0));
    sp.proj = HMM_Perspective_RH_ZO(HMM_AngleDeg(70.0f), aspect, 0.1f, 100.0f);
    sp.sun_dir = HMM_NormV3(HMM_V3(0.4f, 0.8f, 0.3f));
    sp.sun_color = HMM_V3(0.5f, 0.48f, 0.42f);
    sp.ambient = HMM_V3(0.55f, 0.58f, 0.66f);
    sp.sky_horizon = mg_rgb(0xBFE3FF);
    sp.sky_zenith = mg_rgb(0x3F8FE8);
    sp.time = (float)app->time;
    gfx_set_scene(app->gfx, &sp);

    HMM_Mat4 model = HMM_Rotate_RH(HMM_SinF(spin) * 0.7f, HMM_V3(0, 1, 0));
    GfxMaterial m = mat_unlit(HMM_V4(1, 1, 1, 1));
    gfx_draw(app->gfx, g->triangle, &model, &m, 0);

    char buf[64];
    SDL_snprintf(buf, sizeof(buf), "%.0f fps  %.1f ms", app->fps, app->frame_ms);
    text_draw_shadow(app->gfx, &app->font, app->safe_left + 10.0f, app->safe_top + 8.0f, 16.0f,
                     ui_rgba(0xFFFFFFFF), buf);
}

void game_event(MgApp *app, const SDL_Event *e)
{
    (void)app;
    (void)e;
}

void game_pause(MgApp *app) { (void)app; }
void game_resume(MgApp *app) { (void)app; }
void game_quit(MgApp *app) { (void)app; }
