#include "settings_menu.h"

#include <SDL3/SDL_stdinc.h>
#include <string.h>

#include "avatar.h"

#define TILE 56.0f
#define NAME_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-."

static UiColor rgb(uint32_t hex) { return ui_rgba((hex << 8) | 0xFFu); }

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

static void tile_frame(Ui *ui, float x, float y, float w, bool selected, bool held)
{
    if (selected) {
        ui_round_rect(ui->gfx, ui->font, x - 3.0f, y - 3.0f, w + 6.0f, TILE + 6.0f, 15.0f, k_ui_theme.accent);
    }
    ui_round_rect(ui->gfx, ui->font, x, y, w, TILE, 12.0f, held ? k_ui_theme.button_down : ui_rgba(0x2A3042FF));
}

/* One sideways-scrolling row of tiles; returns the index tapped or -1. */
static int tile_row(SettingsMenu *m, Ui *ui, int row, const char *title, float x, float y, float w, int count,
                    int selected, float tile_w)
{
    ui_label(ui, x, y, 15.0f, k_ui_theme.text_dim, title);
    y += 22.0f;
    float off = ui_scroll_begin(ui, &m->rows[row], x - 4.0f, y - 4.0f, w + 8.0f, TILE + 8.0f, true);
    int picked = -1;
    for (int i = 0; i < count; i++) {
        float tx = x + (float)i * (tile_w + 10.0f) - off;
        char id[24];
        SDL_snprintf(id, sizeof(id), "tile%d_%d", row, i);
        bool held = false;
        if (ui_hit_area(ui, id, tx, y, tile_w, TILE, &held)) {
            picked = i;
        }
        tile_frame(ui, tx, y, tile_w, i == selected, held);
        if (row == 0) { /* palette: outfit body, skin head, hair and accent dots */
            AvatarLook l = avatar_look_from_skin((NetSkin){(uint8_t)i, HAIR_SHORT, ACC_NONE});
            float cx = tx + tile_w * 0.5f;
            ui_round_rect(ui->gfx, ui->font, cx - 12.0f, y + 26.0f, 24.0f, 24.0f, 9.0f, rgb(l.outfit));
            ui_circle(ui->gfx, ui->font, cx, y + 17.0f, 10.0f, rgb(l.skin));
            ui_round_rect(ui->gfx, ui->font, cx - 10.0f, y + 6.0f, 20.0f, 8.0f, 4.0f, rgb(l.hair));
            ui_circle(ui->gfx, ui->font, cx + 4.0f, y + 34.0f, 3.5f, rgb(l.accent));
        } else {
            const char *name = row == 1 ? avatar_hair_name(i) : avatar_accessory_name(i);
            text_draw_centered(ui->gfx, ui->font, tx + tile_w * 0.5f, y + TILE * 0.5f, 15.0f, k_ui_theme.text, name);
        }
    }
    ui_scroll_end(ui, &m->rows[row], w, (float)count * (tile_w + 10.0f));
    return picked;
}

static void skin_tab(SettingsMenu *m, Ui *ui, NetSkin *skin, const SettingsAccount *acct, float x, float y, float w,
                     float h, MenuAction *action)
{
    /* left: preview square + name (fixed); right: tile rows + account (scrolls) */
    float left_w = mg_clampf(w * 0.3f, 120.0f, 200.0f);
    float size = mg_minf(left_w, h - 60.0f);
    m->preview_visible = true;
    m->preview_x = x;
    m->preview_y = y;
    m->preview_size = size;
    float ny = y + size + 10.0f;
    ui_label(ui, x, ny, 13.0f, k_ui_theme.text_dim, "Name");
    int ed = ui_text_field(ui, "name", x, ny + 18.0f, left_w, 40.0f, m->name, (int)sizeof(m->name),
                           &m->editing_name, NAME_CHARS);
    if (ed == UI_EDIT_DONE) {
        size_t len = strlen(m->name);
        while (len > 0 && m->name[len - 1] == ' ') {
            m->name[--len] = 0;
        }
        if (len >= 2 && len <= 16) {
            *action = MENU_NAME;
        }
    }

    float rx = x + left_w + 20.0f, rw = w - left_w - 20.0f;
    float off = ui_scroll_begin(ui, &m->skin_scroll, rx - 4.0f, y - 4.0f, rw + 8.0f, h + 4.0f, false);
    float ry = y - off;
    const float row_h = 22.0f + TILE + 16.0f;
    int p = tile_row(m, ui, 0, "Palette", rx, ry, rw, AVATAR_PALETTES, skin->palette, TILE);
    ry += row_h;
    int hr = tile_row(m, ui, 1, "Hair", rx, ry, rw, AVATAR_HAIRS, skin->hair, 76.0f);
    ry += row_h;
    int ac = tile_row(m, ui, 2, "Accessory", rx, ry, rw, AVATAR_ACCESSORIES, skin->accessory, 96.0f);
    ry += row_h;
    if (p >= 0 || hr >= 0 || ac >= 0) {
        skin->palette = p >= 0 ? (uint8_t)p : skin->palette;
        skin->hair = hr >= 0 ? (uint8_t)hr : skin->hair;
        skin->accessory = ac >= 0 ? (uint8_t)ac : skin->accessory;
        *action = MENU_SKIN;
    }

    /* account */
    ui_label(ui, rx, ry, 15.0f, k_ui_theme.text_dim, "Account");
    ry += 22.0f;
    char line[96];
    if (acct->guest) {
        SDL_snprintf(line, sizeof(line), "Signed in as guest");
    } else {
        SDL_snprintf(line, sizeof(line), "Signed in as %s", acct->name);
    }
    ui_label(ui, rx, ry, 16.0f, k_ui_theme.text, line);
    if (!acct->guest && acct->email[0]) {
        ui_label(ui, rx, ry + 20.0f, 13.0f, k_ui_theme.text_dim, acct->email);
    }
    ry += 44.0f;
    float bw = mg_minf(rw, 260.0f);
    if (acct->guest) {
        if (acct->busy) {
            ui_button(ui, "Signing in...##google", rx, ry, bw, 44.0f, false);
        } else if (ui_button(ui, "Continue with Google", rx, ry, bw, 44.0f, acct->google_ready) &&
                   acct->google_ready) {
            *action = MENU_GOOGLE;
        }
    } else if (ui_button(ui, "Sign out", rx, ry, bw, 44.0f, false)) {
        *action = MENU_SIGN_OUT;
    }
    ry += 52.0f;
    const char *msg = acct->message && acct->message[0] ? acct->message
                      : !acct->google_ready            ? "Google sign-in is not configured in this build"
                                                       : "";
    if (msg[0]) {
        ui_label(ui, rx, ry, 13.0f, k_ui_theme.text_dim, msg);
        ry += 22.0f;
    }
    ui_scroll_end(ui, &m->skin_scroll, h, ry + off - y);
}

/* ------------------------------------------------------------------ panel */

MenuAction settings_menu(SettingsMenu *m, Ui *ui, GameSettings *s, NetSkin *skin, const SettingsAccount *acct,
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
    if (ui_button(ui, "Skin##tab1", x + tw + 8.0f, y, tw, 36.0f, m->tab == 1)) {
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
        skin_tab(m, ui, skin, acct, x, y, w, h, &action);
    }
    if (!m->open) {
        m->editing_name = false;
    }
    return action;
}
