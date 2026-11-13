/* The app context: the only global state in the engine. Owns the window,
 * renderer, arenas, input and the 60 Hz fixed-step clock, and forwards SDL3
 * main-callback events to the game. */
#ifndef MG_APP_H
#define MG_APP_H

#include <SDL3/SDL.h>

#include "core/arena.h"
#include "core/time.h"
#include "gfx/gfx.h"
#include "platform/input.h"
#include "ui/text.h"

typedef struct MgApp MgApp;

typedef struct MgGameCallbacks {
    const char *title;
    bool (*init)(MgApp *app);
    void (*tick)(MgApp *app);              /* fixed 60 Hz, dt = MG_TICK_DT */
    void (*frame)(MgApp *app, float alpha); /* render; alpha = tick interpolation */
    void (*event)(MgApp *app, const SDL_Event *e);
    void (*pause)(MgApp *app);  /* entering background */
    void (*resume)(MgApp *app); /* back in foreground */
    void (*quit)(MgApp *app);
} MgGameCallbacks;

struct MgApp {
    SDL_Window *window;
    GfxContext *gfx;
    TextFont font;          /* body: Nunito Bold (UI-1) */
    TextFont font_display;  /* titles, buttons, numbers: Baloo 2 ExtraBold */
    TextFont font_bold;     /* emphasis: Nunito ExtraBold */
    MgInput input;
    Arena perm;  /* lives for the whole run */
    Arena frame; /* reset every frame */
    struct JobSystem *jobs;
    MgFixedStep clock;
    const MgGameCallbacks *cb;
    void *game;

    float width_pt, height_pt; /* drawable size in points */
    float scale;               /* pixels per point */
    float safe_left, safe_top, safe_right, safe_bottom; /* insets, pt */
    float corner_clearance; /* rounded screen corners, pt */
    bool background;
    bool quit_requested;
    bool quit_notified; /* cb->quit already ran */

    double time;      /* seconds since start */
    float frame_dt;   /* seconds since previous frame */
    float fps;        /* smoothed */
    float frame_ms;   /* smoothed frame interval */
    float display_hz;   /* the window's screen at its fastest (ProMotion / VRR: 120 where SDL's mode may say 60), 0 unknown */
    float frame_cap_hz; /* the game's cap (0 none: as fast as the screen goes); iOS skips display-link callbacks, desktop waits */
    float cpu_ms;     /* smoothed CPU time per frame (excludes vsync wait) */
    uint64_t frame_index;
    int ticks_this_frame;

    /* command line (desktop testing flags such as --quit-after <seconds>) */
    int argc;
    char **argv;
    double quit_after;
    double capture_at; /* --metal-capture [S]: capture the frame at S s (0 = off) */
    int orient_request; /* iOS --orientation: 1 left, 2 right, 0 none */
    bool log_events;
};

SDL_AppResult app_init(void **appstate, const MgGameCallbacks *cb, int argc, char **argv);
SDL_AppResult app_iterate(void *appstate);
SDL_AppResult app_event(void *appstate, SDL_Event *e);
void app_quit(void *appstate, SDL_AppResult result);
/* Show / hide the on-screen keyboard; typed text arrives in input.text. */
void app_text_input(MgApp *app, bool on);

/* UI-1: the system's Reduce Motion (iOS accessibility); false elsewhere. */
bool app_reduce_motion(void);

#endif
