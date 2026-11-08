#include "floor_menu.h"

#include <stdio.h>

#define PANEL_W 180.0f
#define PANEL_H 200.0f
#define ROW_H 56.0f

int floor_menu(Ui *ui, float w_pt, float safe_t, float safe_r, int floors, int current,
               const char *(*name)(int floor))
{
    const float gear_bottom = safe_t + 8.0f + 44.0f;
    float x = w_pt - safe_r - 12.0f - PANEL_W;
    float y = gear_bottom + 12.0f;
    gfx_ui_blur_panel(ui->gfx, x, y, PANEL_W, PANEL_H, 16.0f, HMM_V4(0.0f, 0.0f, 0.0f, 0.55f));
    ui_block(ui, x, y, PANEL_W, PANEL_H);

    int picked = -1;
    float pad = (PANEL_H - ROW_H * (float)floors) * 0.5f;
    for (int i = 0; i < floors; i++) {
        int f = floors - 1 - i; /* top floor first */
        float ry = y + pad + ROW_H * (float)i;
        char id[16];
        snprintf(id, sizeof(id), "floor%d", f);
        bool held = false;
        if (f == current) {
            ui_round_rect(ui->gfx, ui->font, x + 8.0f, ry + 4.0f, PANEL_W - 16.0f, ROW_H - 8.0f, 12.0f,
                          ui_rgba(0x4A8BFFCC));
        } else if (ui_hit_area(ui, id, x + 4.0f, ry, PANEL_W - 8.0f, ROW_H, &held)) {
            picked = f;
        }
        if (held) {
            ui_round_rect(ui->gfx, ui->font, x + 8.0f, ry + 4.0f, PANEL_W - 16.0f, ROW_H - 8.0f, 12.0f,
                          ui_rgba(0xFFFFFF33));
        }
        text_draw(ui->gfx, ui->font, x + 22.0f, ry + ROW_H * 0.5f - 11.0f, 19.0f,
                  f == current ? ui_rgba(0xFFFFFFFF) : ui_rgba(0xFFFFFFE6), name(f));
    }
    return picked;
}
