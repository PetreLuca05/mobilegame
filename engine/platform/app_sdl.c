#include "core/jobs.h"
#include "core/prof.h"
#include "platform/device_stats.h"
#include "app.h"

#include "core/log.h"
#include "metal_capture.h"

#define MG_PERM_ARENA_BYTES (96u << 20)
#define MG_FRAME_ARENA_BYTES (4u << 20)
#define MG_MAX_TICKS_PER_FRAME 8

static MgApp g_app; /* the single global app context */

#if defined(SDL_PLATFORM_IOS)
bool ios_safe_insets(SDL_Window *window, float *left, float *top, float *right, float *bottom);
void ios_request_landscape(SDL_Window *window, bool right);
#endif

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
#if defined(SDL_PLATFORM_IOS)
    if (app->safe_left == 0.0f && app->safe_top == 0.0f && app->safe_right == 0.0f && app->safe_bottom == 0.0f) {
        /* SDL had nothing: ask UIKit directly */
        float l, t, r, b;
        if (ios_safe_insets(app->window, &l, &t, &r, &b)) {
            app->safe_left = l;
            app->safe_top = t;
            app->safe_right = r;
            app->safe_bottom = b;
        }
    }
    app->corner_clearance = 6.0f;
#endif
    /* desktop stand-ins for a notched phone, in each landscape direction */
    const char *win_preset = SDL_getenv("MG_WINDOW");
    if (win_preset && SDL_strcmp(win_preset, "iphone") == 0) {
        app->safe_left = 59.0f;
        app->safe_right = 0.0f;
        app->safe_top = 0.0f;
        app->safe_bottom = 21.0f;
    } else if (win_preset && SDL_strcmp(win_preset, "iphone-flip") == 0) {
        app->safe_left = 0.0f;
        app->safe_right = 59.0f;
        app->safe_top = 0.0f;
        app->safe_bottom = 21.0f;
    }
    gfx_set_ui_size(app->gfx, app->width_pt, app->height_pt);
}

SDL_AppResult app_init(void **appstate, const MgGameCallbacks *cb, int argc, char **argv)
{
    MgApp *app = &g_app;
    *appstate = app;
    app->cb = cb;
    app->argc = argc;
    app->argv = argv;
    for (int i = 1; i < argc; i++) {
        if (SDL_strcmp(argv[i], "--quit-after") == 0 && i + 1 < argc) {
            app->quit_after = SDL_atof(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--orientation") == 0 && i + 1 < argc) {
            app->orient_request = SDL_strcmp(argv[++i], "right") == 0 ? 2 : 1;
        } else if (SDL_strcmp(argv[i], "--metal-capture") == 0) {
            app->capture_at = i + 1 < argc && SDL_atof(argv[i + 1]) > 0.0 ? SDL_atof(argv[++i]) : 6.0;
        } else if (SDL_strcmp(argv[i], "--log-events") == 0) {
            app->log_events = true;
        }
    }

    SDL_SetAppMetadata(cb->title, "0.1.0", "ro.docvex.mobilegame");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#if defined(MG_PLATFORM_DESKTOP)
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1"); /* left mouse acts as a finger */
#endif
    prof_init();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        mg_log_error("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
#if defined(MG_PLATFORM_DESKTOP)
    /* re-apply after init: the synthetic "mouse" touch device is registered
     * by the hint callback and must exist once the touch subsystem is up */
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
#endif

    void *mem = SDL_malloc(MG_PERM_ARENA_BYTES); /* the one big allocation */
    if (!mem) {
        return SDL_APP_FAILURE;
    }
    arena_init(&app->perm, mem, MG_PERM_ARENA_BYTES);
    if (!arena_sub(&app->perm, &app->frame, MG_FRAME_ARENA_BYTES)) {
        return SDL_APP_FAILURE;
    }
    mg_log_info("render thread priority: %d", device_boost_render_thread());
    app->jobs = jobs_create(&app->perm, -1, 256u << 10);
    if (!app->jobs) {
        return SDL_APP_FAILURE;
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(MG_PLATFORM_DESKTOP)
    flags |= SDL_WINDOW_RESIZABLE;
    /* landscape phone (iPhone 12-ish) or, with MG_WINDOW=ipad, the iPad aspect */
    const char *win = SDL_getenv("MG_WINDOW");
    bool ipad = win && SDL_strcmp(win, "ipad") == 0;
    app->window = SDL_CreateWindow(cb->title, ipad ? 1180 : 844, ipad ? 820 : 390, flags);
    for (int i = 1; app->window && i + 1 < argc; i++) {
        int wx, wy;
        if (SDL_strcmp(argv[i], "--window-pos") == 0 && SDL_sscanf(argv[i + 1], "%d,%d", &wx, &wy) == 2) {
            SDL_SetWindowPosition(app->window, wx, wy);
        }
    }
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

    prof_frame_begin();
    update_metrics(app);
    arena_reset(&app->frame);
    jobs_frame_begin(app->jobs);

    int ticks = fixedstep_advance(&app->clock, t0, MG_MAX_TICKS_PER_FRAME);
    app->ticks_this_frame = ticks;
    for (int i = 0; i < ticks; i++) {
        if (app->cb->tick) {
            PROF_SCOPE("tick");
            app->cb->tick(app);
        }
    }

    /* one frame into a .gputrace (Xcode shows its GPU time) */
#if defined(SDL_PLATFORM_IOS)
    if (app->orient_request && SDL_GetTicks() > 1000) { /* the window scene exists by now */
        ios_request_landscape(app->window, app->orient_request == 2);
        app->orient_request = 0;
    }
#endif
    bool capture = app->capture_at > 0.0 && (double)SDL_GetTicks() * 0.001 > app->capture_at;
    if (capture) {
        app->capture_at = 0.0;
        capture = metal_capture_begin("frame");
    }
    double wait0 = mg_time_now();
    bool have_frame = gfx_frame_begin(app->gfx);
    double wait1 = mg_time_now();
    if (have_frame) {
        if (app->cb->frame) {
            PROF_SCOPE("frame");
            app->cb->frame(app, fixedstep_alpha(&app->clock));
        }
        PROF_SCOPE("render");
        gfx_frame_end(app->gfx);
    }
    if (capture) {
        metal_capture_end();
    }
    double t1 = mg_time_now();
    smooth(&app->cpu_ms, (float)(((t1 - t0) - (wait1 - wait0)) * 1000.0));
    prof_frame_end((double)dt);
    app->frame_index++;
    input_begin_frame(&app->input); /* clear per-frame edges after the game saw them */
    return SDL_APP_CONTINUE;
}

SDL_AppResult app_event(void *appstate, SDL_Event *e)
{
    MgApp *app = (MgApp *)appstate;
    if (app->log_events && e->type >= SDL_EVENT_KEY_DOWN && e->type <= SDL_EVENT_FINGER_CANCELED) {
        mg_log_info("event 0x%x", (unsigned)e->type);
    }
    switch (e->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_TERMINATING:
        if (app->cb->quit && !app->quit_notified) {
            app->quit_notified = true;
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
    if (app->game && app->cb->quit && !app->quit_notified) { /* every exit path says goodbye */
        app->quit_notified = true;
        app->cb->quit(app);
    }
    if (app->gfx) {
        gfx_shutdown(app->gfx);
    }
    if (app->window) {
        SDL_DestroyWindow(app->window);
    }
    /* perm arena memory is released with the process */
}

void app_text_input(MgApp *app, bool on)
{
    if (!app->window || on == SDL_TextInputActive(app->window)) {
        return;
    }
    if (on) {
        SDL_StartTextInput(app->window);
    } else {
        SDL_StopTextInput(app->window);
    }
}
