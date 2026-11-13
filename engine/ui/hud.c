#include "hud.h"

#include <SDL3/SDL_stdinc.h>

void hud_draw(GfxContext *g, const TextFont *f, float x, float y, const HudInfo *h)
{
    char buf[128];
    const UiColor white = ui_rgba(0xFFFFFFFF);
    const UiColor dim = ui_rgba(0xFFFFFFD0);
    const float line = 17.0f;
    UiColor fps_col = h->fps >= 57.0f ? white : (h->fps >= 45.0f ? ui_rgba(0xFFD166FF) : ui_rgba(0xFF6B6BFF));
    SDL_snprintf(buf, sizeof(buf), "%.0f fps  %.1f ms  cpu %.1f", (double)h->fps, (double)h->frame_ms,
                 (double)h->cpu_ms);
    text_draw_shadow(g, f, x, y, 15.0f, fps_col, buf);
    y += line + 2.0f;
    SDL_snprintf(buf, sizeof(buf), "%s %.1f %.1f %.1f %s", h->l_pos, (double)h->pos.X, (double)h->pos.Y, (double)h->pos.Z,
                 h->grounded ? "" : h->l_air);
    text_draw_shadow(g, f, x, y, 13.0f, dim, buf);
    y += line;
    if (h->ping_ms >= 0) {
        SDL_snprintf(buf, sizeof(buf), "%s %d ms  %s %d", h->l_ping, h->ping_ms, h->l_players, h->players);
    } else {
        SDL_snprintf(buf, sizeof(buf), "%s --  %s %d", h->l_ping, h->l_players, h->players);
    }
    text_draw_shadow(g, f, x, y, 13.0f, dim, buf);
    y += line;
    SDL_snprintf(buf, sizeof(buf), "%s %u  %s %.2f ms  %ux%u", h->l_draws, (unsigned)h->draws, h->l_main, (double)h->main_ms,
                 (unsigned)h->render_w, (unsigned)h->render_h);
    text_draw_shadow(g, f, x, y, 13.0f, dim, buf);
    y += line;
    SDL_snprintf(buf, sizeof(buf), "%s  %s", h->mode ? h->mode : "", h->net ? h->net : "");
    text_draw_shadow(g, f, x, y, 13.0f, dim, buf);
}
