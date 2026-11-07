#include "debug_menu.h"

#include <SDL3/SDL_stdinc.h>
#include <string.h>

static void open_menu(DebugMenu *m, const GameSettings *s)
{
    m->open = true;
    memcpy(m->code, s->room_code, sizeof(m->code));
    m->code_len = (int)strlen(m->code);
}

MenuAction debug_menu(DebugMenu *m, Ui *ui, GameSettings *s, float w_pt, float h_pt, float safe_l, float safe_t,
                      float safe_r, float safe_b, const char *status)
{
    MenuAction action = MENU_NONE;
    /* gear button, top-right */
    const float gs = 44.0f;
    float gx = w_pt - safe_r - 12.0f - gs, gy = safe_t + 8.0f;
    bool held;
    if (ui_hit_area(ui, "gear", gx - 6.0f, gy - 6.0f, gs + 12.0f, gs + 12.0f, &held)) {
        if (m->open) {
            m->open = false;
        } else {
            open_menu(m, s);
        }
    }
    ui_circle(ui->gfx, ui->font, gx + gs * 0.5f, gy + gs * 0.5f, gs * 0.5f,
              held ? ui_rgba(0x1E2230F0) : ui_rgba(0x1E2230A0));
    text_draw_centered(ui->gfx, ui->font, gx + gs * 0.5f, gy + gs * 0.5f + 2.0f, 26.0f, ui_rgba(0xFFFFFFFF),
                       TEXT_GEAR);
    if (!m->open) {
        return MENU_NONE;
    }

    const float pad = 16.0f;
    float px = safe_l + 12.0f, py = gy + gs + 10.0f;
    float pw = w_pt - safe_l - safe_r - 24.0f;
    float ph = h_pt - py - mg_maxf(safe_b, 12.0f) - 8.0f;
    ui_block(ui, 0, 0, w_pt, h_pt); /* modal: nothing reaches the game */
    ui_panel(ui, px, py, pw, ph);
    float x = px + pad, y = py + pad, w = pw - 2 * pad;

    ui_label(ui, x, y, 22.0f, k_ui_theme.text, "Debug");
    if (ui_button(ui, "Close##menu", x + w - 84.0f, y - 4.0f, 84.0f, 34.0f, false)) {
        m->open = false;
    }
    y += 38.0f;
    ui_label(ui, x, y, 13.0f, k_ui_theme.text_dim, status ? status : "");
    y += 24.0f;

    /* room code: 4 slots + A-Z keypad */
    ui_label(ui, x, y + 6.0f, 16.0f, k_ui_theme.text, "Room");
    for (int i = 0; i < 4; i++) {
        float bx = x + 70.0f + (float)i * 44.0f;
        ui_round_rect(ui->gfx, ui->font, bx, y, 38.0f, 36.0f, 8.0f, k_ui_theme.button);
        char c[2] = {i < m->code_len ? m->code[i] : '_', 0};
        text_draw_centered(ui->gfx, ui->font, bx + 19.0f, y + 18.0f, 20.0f, k_ui_theme.text, c);
    }
    y += 46.0f;
    const int cols = 9;
    float kw = (w - (float)(cols - 1) * 4.0f) / (float)cols;
    for (int i = 0; i < 27; i++) {
        float kx = x + (float)(i % cols) * (kw + 4.0f);
        float ky = y + (float)(i / cols) * 40.0f;
        char label[8];
        if (i < 26) {
            SDL_snprintf(label, sizeof(label), "%c##k", 'A' + i);
            if (ui_button(ui, label, kx, ky, kw, 36.0f, false) && m->code_len < 4) {
                m->code[m->code_len++] = (char)('A' + i);
                m->code[m->code_len] = 0;
            }
        } else if (ui_button(ui, "<##del", kx, ky, kw, 36.0f, false) && m->code_len > 0) {
            m->code[--m->code_len] = 0;
        }
    }
    y += 3 * 40.0f + 4.0f;
    float bw = (w - 16.0f) / 3.0f;
    if (ui_button(ui, "Join", x, y, bw, 40.0f, m->code_len == 4) && m->code_len == 4) {
        memcpy(s->room_code, m->code, sizeof(s->room_code));
        action = MENU_JOIN;
        m->open = false;
    }
    if (ui_button(ui, "New room", x + bw + 8.0f, y, bw, 40.0f, false)) {
        action = MENU_NEW_ROOM;
        m->open = false;
    }
    if (ui_button(ui, "Disconnect", x + 2 * (bw + 8.0f), y, bw, 40.0f, false)) {
        action = MENU_DISCONNECT;
        m->open = false;
    }
    y += 54.0f;

    ui_slider(ui, "Camera yaw sens##s1", x, y, w, &s->yaw_deg_per_pt, 0.05f, 0.6f, "%.2f" TEXT_DEGREE "/pt");
    y += 54.0f;
    ui_slider(ui, "Camera pitch sens##s2", x, y, w, &s->pitch_deg_per_pt, 0.05f, 0.5f, "%.2f" TEXT_DEGREE "/pt");
    y += 54.0f;
    ui_slider(ui, "Camera distance##s3", x, y, w, &s->cam_distance, 1.5f, 8.0f, "%.1f m");
    y += 54.0f;
    if (ui_slider(ui, "Camera pitch##s4", x, y, w, &s->cam_pitch_deg, -60.0f, 30.0f, "%.0f" TEXT_DEGREE)) {
        action = action == MENU_NONE ? MENU_CAMERA_PITCH : action;
    }
    y += 54.0f;
    ui_slider(ui, "Field of view##s5", x, y, w, &s->fov_deg, 50.0f, 100.0f, "%.0f" TEXT_DEGREE);
    y += 54.0f;
    ui_slider(ui, "Stick dead zone##s6", x, y, w, &s->dead_zone_pt, 0.0f, 100.0f, "%.0f pt");
    y += 58.0f;
    ui_toggle(ui, "Show colliders", x, y, w, &s->show_colliders);
    y += 42.0f;
    ui_toggle(ui, "Invert Y", x, y, w, &s->invert_y);
    return action;
}
