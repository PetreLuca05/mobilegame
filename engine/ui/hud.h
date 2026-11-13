/* Performance stats block (top-left; Settings -> Arată statisticile de performanță): fps, frame time, position, ping,
 * players. The words come from the game (its string table): the engine only draws them. */
#ifndef MG_HUD_H
#define MG_HUD_H

#include "text.h"

typedef struct HudInfo {
    float fps;
    float frame_ms;
    float cpu_ms;
    HMM_Vec3 pos;
    bool grounded;
    int ping_ms;        /* < 0 = unknown */
    int players;        /* including the local player */
    const char *mode;   /* "offline", "party ABCD", "world 01" */
    const char *net;    /* connection status text */
    uint32_t draws;
    float main_ms;      /* CPU time encoding the main pass (no GPU timestamps in SDL GPU) */
    uint32_t render_w, render_h;
    /* the labels, in the game's language: "poz", "(în aer)", "ping", "jucători", "desene", "codare" */
    const char *l_pos, *l_air, *l_ping, *l_players, *l_draws, *l_main;
} HudInfo;

void hud_draw(GfxContext *g, const TextFont *f, float x, float y, const HudInfo *info);

#endif
