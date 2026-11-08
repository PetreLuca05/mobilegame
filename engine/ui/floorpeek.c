#include "floorpeek.h"

#include <math.h>

#include <SDL3/SDL_stdinc.h>

#define ROW 36.0f
#define GAP 4.0f

int floorpeek_update(FloorPeek *fp, Ui *ui, float x, float h_pt, int floors, int player_floor,
                     const char *const *labels, double now, float hold_s)
{
    float total = (float)floors * ROW + (float)(floors - 1) * GAP;
    float y0 = (h_pt - total) * 0.5f;
    ui_block(ui, x - 4.0f, y0 - 4.0f, ROW + 8.0f, total + 8.0f);
    bool any_held = false;
    for (int i = 0; i < floors; i++) {
        int f = floors - 1 - i; /* top storey first */
        float y = y0 + (float)i * (ROW + GAP);
        char id[16];
        SDL_snprintf(id, sizeof(id), "peek%d", f);
        bool held = false;
        bool tapped = ui_hit_area(ui, id, x - 4.0f, y - GAP * 0.5f, ROW + 8.0f, ROW + GAP, &held);
        if (held) {
            any_held = true;
            fp->peek = f == player_floor ? -1 : f;
        } else if (tapped) {
            fp->peek = f == player_floor || hold_s <= 0.0f ? -1 : f;
            fp->until = now + (double)hold_s;
        }
        bool peeked = fp->peek == f;
        float pulse = peeked ? 0.5f + 0.5f * sinf((float)now * 6.0f) : 0.0f;
        UiColor bg = f == player_floor ? ui_rgba(0x4A8BFFB3)
                     : peeked          ? (UiColor){255, 255, 255, (uint8_t)(60.0f + 70.0f * pulse)}
                                       : ui_rgba(0x00000059);
        ui_round_rect(ui->gfx, ui->font, x, y, ROW, ROW, 10.0f, held ? ui_rgba(0xFFFFFF66) : bg);
        text_draw_centered(ui->gfx, ui->font, x + ROW * 0.5f, y + ROW * 0.5f, 17.0f, ui_rgba(0xFFFFFFFF), labels[f]);
    }
    if (fp->held && !any_held && fp->peek >= 0) { /* finger lifted off a held row */
        fp->until = now + (double)hold_s;
        if (hold_s <= 0.0f) {
            fp->peek = -1;
        }
    }
    fp->held = any_held;
    if (!any_held && fp->peek >= 0 && now >= fp->until) {
        fp->peek = -1;
    }
    return fp->peek;
}
