#include "settings_menu.h"

#include <SDL3/SDL_stdinc.h>
#include <string.h>

#include "avatar.h"
#include "strings.h"

#define NAME_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-."


/* ------------------------------------------------------------------ game */

static float game_tab(SettingsMenu *m, Ui *ui, GameSettings *s, float x, float y, float w, MenuAction *action)
{
    const float y_top = y;
    const float step = 52.0f;
    bool wide = w > 560.0f;
    float cw = wide ? (w - 24.0f) * 0.5f : w;
    float x2 = x + cw + 24.0f;
    /* Day / night: drag to pick an hour (stops following the clock) */
    int hh = (int)s->time_of_day, mm = (int)((s->time_of_day - (float)hh) * 60.0f);
    char tod[64];
    SDL_snprintf(tod, sizeof(tod), "Day / night  %02d:%02d##tod", hh, mm);
    if (ui_slider(ui, tod, x, y, w, &s->time_of_day, 0.0f, 23.99f, "")) {
        *action = MENU_TIME;
    }
    y += 56.0f;
    ui_toggle(ui, "Follow device clock", x, y, cw, &s->follow_clock);
    static const float speeds[3] = {1.0f, 60.0f, 600.0f};
    static const char *const speed_labels[3] = {"x1##spd", "x60##spd", "x600##spd"};
    float sx = wide ? x2 : x, sy = wide ? y : y + 44.0f;
    float sw = (cw - 16.0f) / 3.0f;
    for (int i = 0; i < 3; i++) {
        if (ui_button(ui, speed_labels[i], sx + (float)i * (sw + 8.0f), sy - 2.0f, sw, 40.0f,
                      s->time_speed == speeds[i])) {
            s->time_speed = speeds[i];
        }
    }
    y = sy + 52.0f;
    ui_rect(ui->gfx, ui->font, x, y - 6.0f, w, 1.0f, ui_rgba(0xFFFFFF22));
    y += 6.0f;
    float cx = x, cy = y;
    const float sliders_top = y;
#ifndef MG_CAMERA_THIRDPERSON
    ui_toggle(ui, "Show floor peek", cx, cy, cw, &s->show_floor_peek);
    cy += 44.0f;
    ui_label(ui, cx, cy, 15.0f, k_ui_theme.text_dim, "Floor peek hold time");
    cy += 22.0f;
    static const float holds[3] = {0.0f, 3.0f, 10.0f};
    static const char *const hold_labels[3] = {"0 s##peek", "3 s##peek", "10 s##peek"};
    float hw = (cw - 16.0f) / 3.0f;
    for (int i = 0; i < 3; i++) {
        if (ui_button(ui, hold_labels[i], cx + (float)i * (hw + 8.0f), cy, hw, 40.0f, s->floor_peek_hold == holds[i])) {
            s->floor_peek_hold = holds[i];
        }
    }
    cy += 52.0f;
    ui_toggle(ui, "Show my name tag", cx, cy, cw, &s->show_own_name);
    cy += 44.0f;
    ui_toggle(ui, "Show players through walls", cx, cy, cw, &s->show_xray);
    cy += 44.0f;
#else
    ui_slider(ui, "Camera yaw sens##s1", cx, cy, cw, &s->yaw_deg_per_pt, 0.05f, 0.6f, "%.2f" TEXT_DEGREE "/pt");
    cy += step;
    ui_slider(ui, "Camera pitch sens##s2", cx, cy, cw, &s->pitch_deg_per_pt, 0.05f, 0.5f, "%.2f" TEXT_DEGREE "/pt");
    cy += step;
    if (ui_slider(ui, "Camera pitch##s4", cx, cy, cw, &s->cam_pitch_deg, -60.0f, 30.0f, "%.0f" TEXT_DEGREE)) {
        *action = MENU_CAMERA_PITCH;
    }
    cy += step;
#endif
    float left_end = cy;
    if (wide) {
        cx = x2;
        cy = sliders_top;
    }
#ifdef MG_CAMERA_THIRDPERSON
    ui_slider(ui, "Field of view##s5", cx, cy, cw, &s->fov_deg, 45.0f, 75.0f, "%.0f" TEXT_DEGREE);
    cy += step;
#endif
    ui_slider(ui, "Stick dead zone##s6", cx, cy, cw, &s->dead_zone_pt, 0.0f, 100.0f, "%.0f pt");
    cy += step + 4.0f;
    ui_toggle(ui, "Sound", cx, cy, cw, &s->sound_on);
    cy += 44.0f;
#ifdef MG_CAMERA_THIRDPERSON
    ui_toggle(ui, "Invert Y", cx, cy, cw, &s->invert_y);
    cy += 44.0f;
#endif
    y = mg_maxf(cy, left_end) + 6.0f;


    float bw = (w - 16.0f) / 3.0f;
    char main_menu_label[64];
    SDL_snprintf(main_menu_label, sizeof(main_menu_label), "%s##home", str("save_and_menu"));
    if (ui_button(ui, "Disconnect", x, y, bw, 44.0f, false)) {
        *action = MENU_DISCONNECT;
        m->open = false;
    }
    if (ui_button(ui, main_menu_label, x + bw + 8.0f, y, bw, 44.0f, false)) {
        *action = MENU_MAIN_MENU;
        m->open = false;
    }
    if (ui_button(ui, m->dev_open ? "Developer -##dev" : "Developer +##dev", x + 2.0f * (bw + 8.0f), y, bw, 44.0f,
                  m->dev_open)) {
        m->dev_open = !m->dev_open;
    }
    y += 56.0f;
    if (m->dev_open) {
        ui_toggle(ui, "Show colliders", x, y, cw, &s->show_colliders);
        ui_toggle(ui, "Show triggers", wide ? x2 : x, wide ? y : y + 40.0f, cw, &s->show_triggers);
        y += wide ? 44.0f : 84.0f;
        ui_toggle(ui, "Storey colours", x, y, cw, &s->show_storeys);
        ui_toggle(ui, "X-ray always", wide ? x2 : x, wide ? y : y + 40.0f, cw, &s->xray_always);
        y += wide ? 44.0f : 84.0f;
#ifdef MG_RENDER_FULL
        ui_toggle(ui, "Light gizmos", x, y, cw, &s->show_lights);
        y += 44.0f;
        /* Lighting */
        static const char *const tiers[3] = {"Auto##tier", "Low##tier", "High##tier"};
        float tb = (cw - 16.0f) / 3.0f;
        for (int i = 0; i < 3; i++) {
            if (ui_button(ui, tiers[i], x + (float)i * (tb + 8.0f), y, tb, 40.0f, s->tier_override == i)) {
                s->tier_override = i;
            }
        }
        y += 52.0f;
        ui_toggle(ui, "Shadows", x, y, cw, &s->shadows_on);
        y += 44.0f;
        ui_toggle(ui, "Cascade colours", x, y, cw, &s->show_cascades);
        y += 44.0f;
        ui_toggle(ui, "Freeze shadows", x, y, cw, &s->freeze_shadows);
        y += 44.0f;
#endif
        ui_label(ui, x, y, 15.0f, k_ui_theme.text_dim, "Teleport to floor");
        y += 22.0f;
        static const char *const labels[3] = {"G##tp", "1##tp", "2##tp"};
        float tw = (cw - 16.0f) / 3.0f;
        for (int i = 0; i < 3; i++) {
            if (ui_button(ui, labels[i], x + (float)i * (tw + 8.0f), y, tw, 44.0f, false)) {
                s->teleport_floor = i;
                *action = MENU_TELEPORT;
                m->open = false;
            }
        }
        y += 56.0f;
    }
    return y - y_top;
}

/* ------------------------------------------------------------------ skin */

static void skin_tab(SettingsMenu *m, Ui *ui, const SettingsAccount *acct, float x, float y, float w, float h,
                     MenuAction *action)
{
    (void)h;
    float cw = mg_minf(w, 420.0f);
    m->preview_visible = false;
    ui_label(ui, x, y, 13.0f, k_ui_theme.text_dim, str("username_title"));
    int ed = ui_text_field(ui, "name", x, y + 18.0f, cw, 44.0f, m->name, (int)sizeof(m->name), &m->editing_name,
                           NAME_CHARS);
    if (ed == UI_EDIT_DONE) {
        size_t len = strlen(m->name);
        while (len > 0 && m->name[len - 1] == ' ') {
            m->name[--len] = 0;
        }
        if (len >= 2 && len <= 16) {
            *action = MENU_NAME;
        }
    }
    y += 76.0f;
    char label[64];
    SDL_snprintf(label, sizeof(label), "%s##editchar", str("edit_character"));
    if (ui_button(ui, label, x, y, cw, 48.0f, true)) {
        *action = MENU_EDIT_CHARACTER;
        m->open = false;
    }
    y += 64.0f;
    char line[96];
    if (acct->guest) {
        SDL_snprintf(line, sizeof(line), "%s", str("guest_name"));
    } else {
        SDL_snprintf(line, sizeof(line), "%s %s " TEXT_BULLET " %s", str("signed_in_as"), acct->name,
                     SDL_strcmp(acct->provider, "apple") == 0 ? str("provider_apple") : str("provider_google"));
    }
    ui_label(ui, x, y, 16.0f, k_ui_theme.text, line);
    if (!acct->guest && acct->email[0]) {
        ui_label(ui, x, y + 20.0f, 13.0f, k_ui_theme.text_dim, acct->email);
    }
    y += 44.0f;
    if (acct->guest) {
        if (acct->busy) {
            ui_button(ui, "...##google", x, y, cw, 44.0f, false);
        } else {
            float bw = acct->apple_ready ? (cw - 8.0f) * 0.5f : cw;
            if (acct->google_ready) {
                SDL_snprintf(label, sizeof(label), "%s##google", str("google"));
                if (ui_button(ui, label, x, y, bw, 44.0f, true)) {
                    *action = MENU_GOOGLE;
                }
            }
            if (acct->apple_ready) {
                SDL_snprintf(label, sizeof(label), "%s##apple", str("apple"));
                if (ui_button(ui, label, acct->google_ready ? x + bw + 8.0f : x, y, bw, 44.0f, false)) {
                    *action = MENU_APPLE;
                }
            }
        }
    } else if (ui_button(ui, "Sign out", x, y, cw, 44.0f, false)) {
        *action = MENU_SIGN_OUT;
    }
    y += 52.0f;
    if (acct->message && acct->message[0]) {
        ui_label(ui, x, y, 13.0f, k_ui_theme.text_dim, acct->message);
    }
}

/* ------------------------------------------------------------------ panel */

MenuAction settings_menu(SettingsMenu *m, Ui *ui, GameSettings *s, const SettingsAccount *acct,
                         float w_pt, float h_pt, float safe_l, float safe_t, float safe_r, float safe_b,
                         const char *status)
{
    MenuAction action = MENU_NONE;
    m->preview_visible = false;
    bool held;
    if (!m->open) { /* gear button, top-right */
        const float gs = 44.0f;
        float gx = w_pt - safe_r - 12.0f - gs, gy = safe_t + 8.0f;
        if (ui_hit_area(ui, "gear", gx - 6.0f, gy - 6.0f, gs + 12.0f, gs + 12.0f, &held)) {
            m->open = true;
            SDL_snprintf(m->name, sizeof(m->name), "%s", acct->name);
        }
        ui_circle(ui->gfx, ui->font, gx + gs * 0.5f, gy + gs * 0.5f, gs * 0.5f,
                  held ? ui_rgba(0x1E2230F0) : ui_rgba(0x1E2230A0));
        text_draw_centered(ui->gfx, ui->font, gx + gs * 0.5f, gy + gs * 0.5f + 2.0f, 26.0f, ui_rgba(0xFFFFFFFF),
                           TEXT_GEAR);
        m->editing_name = false;
        if (!m->open) {
            return MENU_NONE;
        }
    }
    if (!m->editing_name && strcmp(m->name, acct->name) != 0 && action == MENU_NONE) {
        /* keep in sync with the account unless the player is typing */
        SDL_snprintf(m->name, sizeof(m->name), "%s", acct->name);
    }

    /* full screen, two columns: back + title + tabs left (35 %), content right (65 %) */
    const float pad = 16.0f;
    ui_block(ui, 0, 0, w_pt, h_pt); /* modal: nothing reaches the game */
    gfx_ui_blur_panel(ui->gfx, 0.0f, 0.0f, w_pt, h_pt, 0.0f, HMM_V4(0.05f, 0.06f, 0.09f, 0.82f));
    float x0 = safe_l + pad, y0 = mg_maxf(safe_t, 8.0f) + 8.0f;
    float inner_w = w_pt - safe_l - safe_r - 2.0f * pad;
    float bottom = h_pt - mg_maxf(safe_b, 12.0f);
    float lw = inner_w * 0.35f - pad;
    if (ui_hit_area(ui, "back##menu", x0 - 6.0f, y0 - 6.0f, 56.0f, 56.0f, &held)) {
        m->open = false;
    }
    ui_circle(ui->gfx, ui->font, x0 + 22.0f, y0 + 22.0f, 22.0f, held ? ui_rgba(0xFFFFFF40) : ui_rgba(0xFFFFFF1A));
    text_draw_centered(ui->gfx, ui->font, x0 + 20.0f, y0 + 22.0f, 26.0f, ui_rgba(0xFFFFFFFF), "<");
    ui_label(ui, x0 + 58.0f, y0 + 8.0f, 22.0f, k_ui_theme.text, "Settings");
    float y = y0 + 64.0f;
    if (ui_button(ui, "Game##tab0", x0, y, lw, 44.0f, m->tab == 0)) {
        m->tab = 0;
    }
    y += 54.0f;
    if (ui_button(ui, "Account##tab1", x0, y, lw, 44.0f, m->tab == 1)) {
        m->tab = 1;
    }
    y += 60.0f;
    if (status && status[0]) {
        ui_label(ui, x0, y, 13.0f, k_ui_theme.text_dim, status);
    }

    float x = x0 + inner_w * 0.35f + pad, w = inner_w * 0.65f - pad;
    y = y0 + 4.0f;
    float h = bottom - y;
    if (m->tab == 0) {
        m->editing_name = false;
        float off = ui_scroll_begin(ui, &m->game_scroll, x - 8.0f, y - 4.0f, w + 16.0f, h + 8.0f, false);
        float used = game_tab(m, ui, s, x, y - off, w, &action);
        ui_scroll_end(ui, &m->game_scroll, h, used);
    } else {
        skin_tab(m, ui, acct, x, y, w, h, &action);
    }
    if (!m->open) {
        m->editing_name = false;
    }
    return action;
}
