#include "launch_screen.h"

#include <string.h>

LaunchChoice launch_screen(LaunchScreen *ls, Ui *ui, float w, float h, float safe_l, float safe_t, float safe_r,
                           float safe_b, bool online_available)
{
    LaunchChoice choice = LAUNCH_NONE;
    ui_block(ui, 0, 0, w, h); /* the game underneath gets no touches */

    /* one centred column, clear of the notch in landscape */
    float left = safe_l + 24.0f, right = w - safe_r - 24.0f;
    float bw = mg_minf(right - left, 440.0f);
    float pad = (left + right - bw) * 0.5f; /* x of the column */
    float cx = pad + bw * 0.5f;
    float bh = 62.0f, kh = 44.0f;
    float bottom = h - (safe_b > 16.0f ? safe_b : 16.0f) - 24.0f;
    float menu_y = bottom - 3.0f * (bh + 14.0f) - 30.0f;
    float code_y = bottom - (3.0f * (kh + 4.0f) + 58.0f + 64.0f + 70.0f);

    /* title, unless the panel needs the room (code entry in landscape) */
    float ty = safe_t + h * 0.12f;
    if (ty + 64.0f < (ls->page == 2 ? code_y : menu_y) - 18.0f) {
        UiColor shadow = ui_rgba(0x1E223099);
        text_draw_centered(ui->gfx, ui->font, cx + 2.0f, ty + 3.0f, 48.0f, shadow, "Mobilegame");
        text_draw_centered(ui->gfx, ui->font, cx, ty, 48.0f, ui_rgba(0xFFFFFFFF), "Mobilegame");
        text_draw_centered(ui->gfx, ui->font, cx, ty + 44.0f, 18.0f, ui_rgba(0xFFFFFFE0), "M1 " TEXT_BULLET " Dev Room");
    }

    if (ls->page == 0) {
        float y = menu_y;
        ui_round_rect(ui->gfx, ui->font, pad - 12.0f, y - 18.0f, bw + 24.0f, bottom - y + 36.0f, 22.0f,
                      ui_rgba(0x1E2230B8));
        if (ui_button(ui, "Play offline", pad, y, bw, bh, false)) {
            choice = LAUNCH_OFFLINE;
        }
        y += bh + 14.0f;
        if (ui_button(ui, "Private party", pad, y, bw, bh, true) && online_available) {
            ls->page = 1;
        }
        y += bh + 14.0f;
        if (ui_button(ui, "Open world", pad, y, bw, bh, false) && online_available) {
            choice = LAUNCH_WORLD;
        }
        y += bh + 10.0f;
        text_draw_centered(ui->gfx, ui->font, cx, y + 8.0f, 13.0f, ui_rgba(0xFFFFFFB0),
                           online_available ? "Party: 2-4 friends with a room code " TEXT_BULLET " World: public rooms"
                                            : "Online play is not configured in this build");
    } else if (ls->page == 1) {
        float y = menu_y;
        ui_round_rect(ui->gfx, ui->font, pad - 12.0f, y - 18.0f, bw + 24.0f, bottom - y + 36.0f, 22.0f,
                      ui_rgba(0x1E2230B8));
        if (ui_button(ui, "Create room", pad, y, bw, bh, true)) {
            choice = LAUNCH_CREATE_PARTY;
        }
        y += bh + 14.0f;
        if (ui_button(ui, "Join with code", pad, y, bw, bh, false)) {
            ls->page = 2;
            ls->code[0] = 0;
        }
        y += bh + 14.0f;
        if (ui_button(ui, "Back##party", pad, y, bw, bh, false)) {
            ls->page = 0;
        }
    } else {
        float y = code_y;
        ui_round_rect(ui->gfx, ui->font, pad - 12.0f, y - 18.0f, bw + 24.0f, bottom - y + 36.0f, 22.0f,
                      ui_rgba(0x1E2230D0));
        text_draw_centered(ui->gfx, ui->font, cx, y + 10.0f, 18.0f, ui_rgba(0xFFFFFFFF), "Enter the room code");
        y += 34.0f;
        size_t len = strlen(ls->code);
        float sw = 56.0f, gap = 12.0f, sx = cx - (4.0f * sw + 3.0f * gap) * 0.5f;
        for (int i = 0; i < 4; i++) {
            float bx = sx + (float)i * (sw + gap);
            ui_round_rect(ui->gfx, ui->font, bx, y, sw, 56.0f, 12.0f,
                          (size_t)i == len ? ui_rgba(0x4A8BFF66) : ui_rgba(0xFFFFFF2A));
            char c[2] = {(size_t)i < len ? ls->code[i] : 0, 0};
            text_draw_centered(ui->gfx, ui->font, bx + sw * 0.5f, y + 28.0f, 30.0f, ui_rgba(0xFFFFFFFF), c);
        }
        y += 70.0f;
        y += ui_letter_keypad(ui, "launch", pad, y, bw, kh, ls->code, 4) + 10.0f;
        float hw = (bw - 10.0f) * 0.5f;
        if (ui_button(ui, "Back##join", pad, y, hw, bh, false)) {
            ls->page = 1;
        }
        if (ui_button(ui, "Join##code", pad + hw + 10.0f, y, hw, bh, len == 4) && len == 4) {
            choice = LAUNCH_JOIN_PARTY;
        }
    }
    if (choice != LAUNCH_NONE) {
        ls->active = false;
        ls->page = 0;
    }
    return choice;
}
