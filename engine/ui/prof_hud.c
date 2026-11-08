#include "prof_hud.h"

#include <SDL3/SDL_stdinc.h>

#include "core/prof.h"

void prof_hud_draw(GfxContext *g, const TextFont *f, float x, float y, const ProfHudInfo *h)
{
    static const char *const thermal[4] = {"nominal", "fair", "serious", "critical"};
    const UiColor white = ui_rgba(0xFFFFFFFF), dim = ui_rgba(0xFFFFFFD0), red = ui_rgba(0xFF6B6BFF);
    const float line = 14.0f, size = 12.0f;
    char buf[128];
    SDL_snprintf(buf, sizeof(buf), "draws %u  inst %u  tris %uk  passes %u", h->draws, h->instances, h->tris / 1000, h->passes);
    text_draw_shadow(g, f, x, y, size, white, buf);
    y += line;
    SDL_snprintf(buf, sizeof(buf), "pipes %u  tex %u  uniforms %u KB", h->pipe_binds, h->tex_binds, h->uniform_bytes / 1024);
    text_draw_shadow(g, f, x, y, size, dim, buf);
    y += line;
    SDL_snprintf(buf, sizeof(buf), "entities %u sim  %u drawn", h->sim, h->rendered);
    text_draw_shadow(g, f, x, y, size, dim, buf);
    y += line;
    SDL_snprintf(buf, sizeof(buf), "GPU approx: main %.1f  UI %.1f ms", (double)h->gpu_main_ms, (double)h->gpu_ui_ms);
    text_draw_shadow(g, f, x, y, size, dim, buf);
    y += line;
    SDL_snprintf(buf, sizeof(buf), "thermal %s  mem %.0f MB", h->thermal >= 0 && h->thermal < 4 ? thermal[h->thermal] : "n/a",
                 (double)h->resident_mb);
    text_draw_shadow(g, f, x, y, size, h->thermal >= 2 ? red : dim, buf);
    y += line;
    uint32_t allocs = prof_allocs_last_frame();
    SDL_snprintf(buf, sizeof(buf), "heap allocs this frame: %u", allocs);
    text_draw_shadow(g, f, x, y, size, allocs ? red : dim, buf);
    y += line + 3.0f;
    text_draw_shadow(g, f, x, y, size, white, "scope               avg    p95    max ms");
    y += line;
    ProfRow rows[12];
    int n = prof_top(rows, 12);
    for (int i = 0; i < n; i++) {
        SDL_snprintf(buf, sizeof(buf), "%*s%-*s %5.2f  %5.2f  %5.2f", rows[i].depth, "", 18 - rows[i].depth, rows[i].name,
                     (double)rows[i].avg_ms, (double)rows[i].p95_ms, (double)rows[i].max_ms);
        text_draw_shadow(g, f, x, y, size, dim, buf);
        y += line;
    }
}
