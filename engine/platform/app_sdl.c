#include "app.h"

#include "core/log.h"

#define MG_PERM_ARENA_BYTES (96u << 20)
#define MG_FRAME_ARENA_BYTES (4u << 20)
#define MG_MAX_TICKS_PER_FRAME 8

static MgApp g_app; /* the single global app context */

static void update_metrics(MgApp *app)
{
    int pw = 0, ph = 0, ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(app->window, &pw, &ph);
    SDL_GetWindowSize(app->window, &ww, &wh);
    float scale = SDL_GetWindowDisplayScale(app->window);
    if (scale <= 0.0f) {
        scale = 1.0f;
    }
    app->scale = scale;
    app->width_pt = (float)pw / scale;
    app->height_pt = (float)ph / scale;
    SDL_Rect safe;
    if (ww > 0 && SDL_GetWindowSafeArea(app->window, &safe)) {
        float f = app->width_pt / (float)ww; /* window units -> pt */
        app->safe_left = (float)safe.x * f;
        app->safe_top = (float)safe.y * f;
        app->safe_right = (float)(ww - safe.x - safe.w) * f;
        app->safe_bottom = (float)(wh - safe.y - safe.h) * f;
    }
    gfx_set_ui_size(app->gfx, app->width_pt, app->height_pt);
}

SDL_AppResult app_init(void **appstate, const MgGameCallbacks *cb, int argc, char **argv)
{
    MgApp *app = &g_app;
    *appstate = app;
    app->cb = cb;
    for (int i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--quit-after") == 0 && i + 1 < argc) {
            app->quit_after = SDL_atof(argv[++i]);
        }
    }

    SDL_SetAppMetadata(cb->title, "0.1.0", "ro.docvex.mobilegame");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#if defined(MG_PLATFORM_DESKTOP)
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1"); /* left mouse acts as a finger */
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        mg_log_error("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    void *mem = SDL_malloc(MG_PERM_ARENA_BYTES); /* the one big allocation */
    if (!mem) {
        return SDL_APP_FAILURE;
    }
    arena_init(&app->perm, mem, MG_PERM_ARENA_BYTES);
    if (!arena_sub(&app->perm, &app->frame, MG_FRAME_ARENA_BYTES)) {
        return SDL_APP_FAILURE;
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(MG_PLATFORM_DESKTOP)
    flags |= SDL_WINDOW_RESIZABLE;
    app->window = SDL_CreateWindow(cb->title, 390, 844, flags);
#else
    flags |= SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS;
    app->window = SDL_CreateWindow(cb->title, 0, 0, flags);
#endif
    if (!app->window) {
        mg_log_error("SDL_CreateWindow failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (!gfx_init(&app->gfx, app->window, &app->perm)) {
        return SDL_APP_FAILURE;
    }
    if (!text_init(&app->font, app->gfx)) {
        return SDL_APP_FAILURE;
    }
    for (int i = 0; i < MG_MAX_TOUCHES; i++) {
        app->input.touches[i].owner = MG_TOUCH_FREE;
    }
    update_metrics(app);
    fixedstep_init(&app->clock, MG_TICK_HZ);
    if (cb->init && !cb->init(app)) {
        return SDL_APP_FAILURE;
    }
    mg_log_info("app: %.0fx%.0f pt @ %.2fx, safe insets l%.0f t%.0f r%.0f b%.0f, perm arena %zu KB used",
                app->width_pt, app->height_pt, app->scale, app->safe_left, app->safe_top, app->safe_right,
                app->safe_bottom, app->perm.used / 1024);
    return SDL_APP_CONTINUE;
}

static void smooth(float *v, float x)
{
    *v = (*v == 0.0f) ? x : (*v * 0.9f + x * 0.1f);
}

SDL_AppResult app_iterate(void *appstate)
{
    MgApp *app = (MgApp *)appstate;
    if (app->quit_requested) {
        return SDL_APP_SUCCESS;
    }
    if (app->background) {
        SDL_Delay(50);
        return SDL_APP_CONTINUE;
    }
    double t0 = mg_time_now();
    float dt = (float)(t0 - (app->time > 0.0 ? app->time : t0));
    app->time = t0;
    app->frame_dt = dt;
    if (dt > 0.0f) {
        smooth(&app->frame_ms, dt * 1000.0f);
        app->fps = app->frame_ms > 0.0f ? 1000.0f / app->frame_ms : 0.0f;
    }
    if (app->quit_after > 0.0 && (double)SDL_GetTicks() * 0.001 > app->quit_after) {
        return SDL_APP_SUCCESS;
    }

    update_metrics(app);
    arena_reset(&app->frame);

    int ticks = fixedstep_advance(&app->clock, t0, MG_MAX_TICKS_PER_FRAME);
    app->ticks_this_frame = ticks;
    for (int i = 0; i < ticks; i++) {
        if (app->cb->tick) {
            app->cb->tick(app);
        }
    }

    double wait0 = mg_time_now();
    bool have_frame = gfx_frame_begin(app->gfx);
    double wait1 = mg_time_now();
    if (have_frame) {
        if (app->cb->frame) {
            app->cb->frame(app, fixedstep_alpha(&app->clock));
        }
        gfx_frame_end(app->gfx);
    }
    double t1 = mg_time_now();
    smooth(&app->cpu_ms, (float)(((t1 - t0) - (wait1 - wait0)) * 1000.0));
    app->frame_index++;
    input_begin_frame(&app->input); /* clear per-frame edges after the game saw them */
    return SDL_APP_CONTINUE;
}

SDL_AppResult app_event(void *appstate, SDL_Event *e)
{
    MgApp *app = (MgApp *)appstate;
    switch (e->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_TERMINATING:
        if (app->cb->quit) {
            app->cb->quit(app);
        }
        return SDL_APP_SUCCESS;
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
        if (!app->background) {
            app->background = true;
            gfx_set_paused(app->gfx, true);
            if (app->cb->pause) {
                app->cb->pause(app);
            }
        }
        return SDL_APP_CONTINUE;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        if (app->background) {
            app->background = false;
            gfx_set_paused(app->gfx, false);
            fixedstep_reset(&app->clock);
            app->time = 0.0;
            if (app->cb->resume) {
                app->cb->resume(app);
            }
        }
        return SDL_APP_CONTINUE;
    case SDL_EVENT_KEY_DOWN:
        if (e->key.scancode == SDL_SCANCODE_ESCAPE) {
            return SDL_APP_SUCCESS;
        }
        break;
    default:
        break;
    }
    input_handle_event(&app->input, e, app->width_pt, app->height_pt);
    if (app->cb->event) {
        app->cb->event(app, e);
    }
    return SDL_APP_CONTINUE;
}

void app_quit(void *appstate, SDL_AppResult result)
{
    (void)result;
    MgApp *app = (MgApp *)appstate;
    if (!app) {
        return;
    }
    if (app->gfx) {
        gfx_shutdown(app->gfx);
    }
    if (app->window) {
        SDL_DestroyWindow(app->window);
    }
    /* perm arena memory is released with the process */
}
