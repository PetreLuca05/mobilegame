/* Developer -> Profiler: counters and the top scopes (avg / p95 / max over 120 frames). */
#ifndef MG_PROF_HUD_H
#define MG_PROF_HUD_H

#include "text.h"

typedef struct ProfHudInfo {
    uint32_t draws, tris, instances, pipe_binds, tex_binds, uniform_bytes, passes;
    uint32_t sim, rendered; /* entities */
    float gpu_main_ms, gpu_ui_ms; /* approx: submit -> fence */
    int thermal;            /* -1 unknown */
    float resident_mb;
} ProfHudInfo;

void prof_hud_draw(GfxContext *g, const TextFont *f, float x, float y, const ProfHudInfo *info);

#endif
