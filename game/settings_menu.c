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
    float cx = x, cy = y;
    ui_slider(ui, "Camera yaw sens##s1", cx, cy, cw, &s->yaw_deg_per_pt, 0.05f, 0.6f, "%.2f" TEXT_DEGREE "/pt");
    cy += step;
    ui_slider(ui, "Camera pitch sens##s2", cx, cy, cw, &s->pitch_deg_per_pt, 0.05f, 0.5f, "%.2f" TEXT_DEGREE "/pt");
    cy += step;
    ui_slider(ui, "Camera distance##s3", cx, cy, cw, &s->cam_distance, 1.5f, 8.0f, "%.1f m");
    cy += step;
    if (ui_slider(ui, "Camera pitch##s4", cx, cy, cw, &s->cam_pitch_deg, -60.0f, 30.0f, "%.0f" TEXT_DEGREE)) {
        *action = MENU_CAMERA_PITCH;
    }
    cy += step;
    float left_end = cy;
    if (wide) {
        cx = x2;
        cy = y;
    }
    ui_slider(ui, "Field of view##s5", cx, cy, cw, &s->fov_deg, 50.0f, 100.0f, "%.0f" TEXT_DEGREE);
    cy += step;
    ui_slider(ui, "Stick dead zone##s6", cx, cy, cw, &s->dead_zone_pt, 0.0f, 100.0f, "%.0f pt");
    cy += step;
    ui_slider(ui, "Render scale##s7", cx, cy, cw, &s->render_scale, 0.5f, 1.0f, "%.2f");
    cy += step + 4.0f;
    ui_toggle(ui, "Invert Y", cx, cy, cw, &s->invert_y);
    cy += 44.0f;
    y = mg_maxf(cy, left_end) + 6.0f;

    float bw = (w - 16.0f) / 3.0f;
    if (ui_button(ui, "Disconnect", x, y, bw, 44.0f, false)) {
        *action = MENU_DISCONNECT;
        m->open = false;
    }
    if (ui_button(ui, "Main menu##home", x + bw + 8.0f, y, bw, 44.0f, false)) {
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
        /* Time */
        ui_toggle(ui, "Follow device clock", x, y, cw, &s->follow_clock);
        y += 44.0f;
        if (ui_slider(ui, "Time of day##tod", x, y, cw, &s->time_of_day, 0.0f, 23.99f, "%.2f h")) {
            *action = MENU_TIME;
        }
        y += 56.0f;
        static const float speeds[3] = {1.0f, 60.0f, 600.0f};
        static const char *const speed_labels[3] = {"x1##spd", "x60##spd", "x600##spd"};
        float sw = (cw - 16.0f) / 3.0f;
        for (int i = 0; i < 3; i++) {
            if (ui_button(ui, speed_labels[i], x + (float)i * (sw + 8.0f), y, sw, 40.0f, s->time_speed == speeds[i])) {
                s->time_speed = speeds[i];
            }
        }
        y += 52.0f;
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
    /* gear button, top-right */
    const float gs = 44.0f;
    float gx = w_pt - safe_r - 12.0f - gs, gy = safe_t + 8.0f;
    bool held;
    if (ui_hit_area(ui, "gear", gx - 6.0f, gy - 6.0f, gs + 12.0f, gs + 12.0f, &held)) {
        m->open = !m->open;
        if (m->open) {
            SDL_snprintf(m->name, sizeof(m->name), "%s", acct->name);
        }
    }
    ui_circle(ui->gfx, ui->font, gx + gs * 0.5f, gy + gs * 0.5f, gs * 0.5f,
              held ? ui_rgba(0x1E2230F0) : ui_rgba(0x1E2230A0));
    text_draw_centered(ui->gfx, ui->font, gx + gs * 0.5f, gy + gs * 0.5f + 2.0f, 26.0f, ui_rgba(0xFFFFFFFF),
                       TEXT_GEAR);
    if (!m->open) {
        m->editing_name = false;
        return MENU_NONE;
    }
    if (!m->editing_name && strcmp(m->name, acct->name) != 0 && action == MENU_NONE) {
        /* keep in sync with the account unless the player is typing */
        SDL_snprintf(m->name, sizeof(m->name), "%s", acct->name);
    }

    const float pad = 16.0f;
    float px = safe_l + 12.0f, py = gy + gs + 10.0f;
    float pw = w_pt - safe_l - safe_r - 24.0f;
    float ph = h_pt - py - mg_maxf(safe_b, 12.0f) - 8.0f;
    ui_block(ui, 0, 0, w_pt, h_pt); /* modal: nothing reaches the game */
    ui_panel(ui, px, py, pw, ph);
    float x = px + pad, y = py + pad, w = pw - 2 * pad;

    ui_label(ui, x, y, 22.0f, k_ui_theme.text, "Settings");
    if (status && status[0]) {
        ui_label(ui, x + 110.0f, y + 6.0f, 13.0f, k_ui_theme.text_dim, status);
    }
    if (ui_button(ui, "Close##menu", x + w - 84.0f, y - 4.0f, 84.0f, 34.0f, false)) {
        m->open = false;
    }
    y += 40.0f;
    float tw = (w - 8.0f) * 0.5f;
    if (ui_button(ui, "Game##tab0", x, y, tw, 36.0f, m->tab == 0)) {
        m->tab = 0;
    }
    if (ui_button(ui, "Account##tab1", x + tw + 8.0f, y, tw, 36.0f, m->tab == 1)) {
        m->tab = 1;
    }
    y += 48.0f;
    float h = py + ph - pad - y;
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
