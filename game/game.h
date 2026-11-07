#ifndef MG_GAME_H
#define MG_GAME_H

#include "platform/app.h"

bool game_init(MgApp *app);
void game_tick(MgApp *app);
void game_frame(MgApp *app, float alpha);
void game_event(MgApp *app, const SDL_Event *e);
void game_pause(MgApp *app);
void game_resume(MgApp *app);
void game_quit(MgApp *app);

#endif
