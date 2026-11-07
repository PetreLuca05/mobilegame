/* Entry point: SDL3 main callbacks forwarded to the engine app layer. */
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>

#include "game.h"
#include "platform/app.h"

static const MgGameCallbacks k_game = {
    .title = "Mobilegame",
    .init = game_init,
    .tick = game_tick,
    .frame = game_frame,
    .event = game_event,
    .pause = game_pause,
    .resume = game_resume,
    .quit = game_quit,
};

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) { return app_init(appstate, &k_game, argc, argv); }

SDL_AppResult SDL_AppIterate(void *appstate) { return app_iterate(appstate); }

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) { return app_event(appstate, event); }

void SDL_AppQuit(void *appstate, SDL_AppResult result) { app_quit(appstate, result); }
