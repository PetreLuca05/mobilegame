#include "ui/layout.h"
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
#ifdef MG_DAYCYCLE_CONTINUOUS
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
#endif
    float cx = x, cy = y;
    const float sliders_top = y;
#ifndef MG_CAMERA_THIRDPERSON
    ui_toggle(ui, str("show_stats"), cx, cy, cw, &s->show_stats);
    cy += 44.0f;
    ui_label(ui, cx, cy, 15.0f, k_ui_theme.text_dim, str("chat_bubbles"));
    cy += 22.0f;
    static const char *const bubble_keys[3] = {"npc_off##bub", "bubble_active##bub", "npc_all##bub"};
    const float bub_w = (cw - 16.0f) / 3.0f;
    for (int i = 0; i < 3; i++) {
        if (ui_button(ui, str(bubble_keys[i]), cx + (float)i * (bub_w + 8.0f), cy, bub_w, 40.0f, s->chat_bubbles == i)) {
            s->chat_bubbles = i;
        }
    }
    cy += 52.0f;
    ui_label(ui, cx, cy, 15.0f, k_ui_theme.text_dim, str("npc_density"));
    cy += 22.0f;
    static const float npc_d[3] = {0.0f, 0.5f, 1.0f};
    static const char *const npc_d_keys[3] = {"npc_off##den", "npc_low##den", "npc_normal##den"};
    const float third = (cw - 16.0f) / 3.0f;
    for (int i = 0; i < 3; i++) {
        if (ui_button(ui, str(npc_d_keys[i]), cx + (float)i * (third + 8.0f), cy, third, 40.0f, s->npc_density == npc_d[i])) {
            s->npc_density = npc_d[i];
        }
    }
    cy += 52.0f;
    ui_toggle(ui, "Show my name tag", cx, cy, cw, &s->show_own_name);
    cy += 44.0f;
    ui_toggle(ui, "Show players through walls", cx, cy, cw, &s->show_xray);
    cy += 44.0f;
    ui_toggle(ui, str("haptics"), cx, cy, cw, &s->haptics);
    cy += 44.0f;
    ui_toggle(ui, str("marker_sounds"), cx, cy, cw, &s->marker_sounds);
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
    ui_slider(ui, "Stick dead zone##s6", cx, cy, cw, &s->dead_zone_pt, 0.0f, 50.0f, "%.0f pt");
    cy += step + 4.0f;
    ui_toggle(ui, "Sound", cx, cy, cw, &s->sound_on);
    cy += 44.0f;
#ifdef MG_CAMERA_THIRDPERSON
    ui_toggle(ui, "Invert Y", cx, cy, cw, &s->invert_y);
    cy += 44.0f;
#endif
    y = mg_maxf(cy, left_end) + 6.0f;


    if (ui_button(ui, m->dev_open ? "Developer -##dev" : "Developer +##dev", x, y, cw, 44.0f, m->dev_open)) {
        m->dev_open = !m->dev_open;
    }
    y += 56.0f;
    if (m->dev_open) {
        ui_toggle(ui, "Show colliders", x, y, cw, &s->show_colliders);
        ui_toggle(ui, "Show triggers", wide ? x2 : x, wide ? y : y + 40.0f, cw, &s->show_triggers);
        y += wide ? 44.0f : 84.0f;
        ui_toggle(ui, "Nav grid", x, y + (wide ? 44.0f : 84.0f), cw, &s->show_nav);
        ui_toggle(ui, "Storey colours", x, y, cw, &s->show_storeys);
        ui_toggle(ui, "X-ray always", wide ? x2 : x, wide ? y : y + 40.0f, cw, &s->xray_always);
        y += (wide ? 44.0f : 84.0f) + 44.0f;
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
        ui_toggle(ui, "Performance stats##dev_perf", x, y, cw, &s->show_stats);
        y += 44.0f;
        ui_toggle(ui, "Force elevator panel", x, y, cw, &s->force_lift_panel);
        y += 44.0f;
        ui_toggle(ui, "Show ranges", x, y, cw, &s->show_ia_ranges);
        y += 44.0f;
        ui_toggle(ui, "Show active id", x, y, cw, &s->show_ia_active);
        y += 44.0f;
        ui_toggle(ui, "Preview any mesh", x, y, cw, &s->preview_any);
        y += 44.0f;
        ui_toggle(ui, "Show safe rect", x, y, cw, &s->show_safe_rect);
        y += 44.0f;
        ui_toggle(ui, "Profiler", x, y, cw, &s->show_profiler);
        y += 44.0f;
        ui_toggle(ui, "Show slots", x, y, cw, &s->show_slots);
        y += 44.0f;
        ui_toggle(ui, "Show NPC packages", x, y, cw, &s->show_packages);
        y += 44.0f;
        ui_toggle(ui, "Show NPC paths", x, y, cw, &s->show_npc_paths);
        y += 44.0f;
        ui_toggle(ui, "Freeze NPCs", x, y, cw, &s->freeze_npcs);
        y += 44.0f;
        if (ui_button(ui, "Respawn NPCs##npc", x, y, cw, 40.0f, false)) {
            s->respawn_npcs = true;
        }
        y += 52.0f;
        ui_label(ui, x, y, 15.0f, k_ui_theme.text_dim, "Stress");
        y += 22.0f;
        ui_slider(ui, "Dummy avatars##stress", x, y, cw, &s->stress_count, 0.0f, 400.0f, "%.0f");
        y += step;
        ui_label(ui, x, y, 15.0f, k_ui_theme.text_dim, "Markers");
        y += 22.0f;
        ui_toggle(ui, "Test markers", x, y, cw, &s->test_markers);
        y += 44.0f;
        ui_slider(ui, "Marker count##mkc", x, y, cw, &s->test_marker_count, 1.0f, 16.0f, "%.0f");
        y += step;
        ui_toggle(ui, "Show paths for all markers", x, y, cw, &s->show_all_paths);
        y += 44.0f;
        if (ui_button(ui, "Respawn all##mkr", x, y, cw, 40.0f, false)) {
            *action = MENU_RESPAWN_MARKERS;
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

/* Returns the height used. */
static float skin_tab(SettingsMenu *m, Ui *ui, const SettingsAccount *acct, float x, float y, float w, MenuAction *action)
{
    const float y_start = y;
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
    if (acct->guest) {
        ui_label(ui, x, y, 15.0f, k_ui_theme.text, str("link_guest_line"));
    } else {
        const float hs = 44.0f;
        if (m->heads) {
            head_icon_draw(m->heads, ui->gfx, ui->font, m->my_skin, m->my_name, x + hs * 0.5f, y + 14.0f, hs, false);
        }
        char line[96];
        SDL_snprintf(line, sizeof(line), "%s " TEXT_BULLET " %s", acct->name, SDL_strcmp(acct->provider, "apple") == 0 ? str("provider_apple") : str("provider_google"));
        ui_label(ui, x + hs + 10.0f, y - 4.0f, 16.0f, k_ui_theme.text, line);
        ui_label(ui, x + hs + 10.0f, y + 18.0f, 13.0f, k_ui_theme.text_dim, str("link_connected"));
    }
    y += 56.0f;
    if (acct->guest) {
        if (acct->busy) {
            ui_button(ui, "...##google", x, y, cw, 44.0f, false);
        } else {
            float bw = acct->apple_ready ? (cw - 8.0f) * 0.5f : cw;
            if (acct->google_ready) {
                SDL_snprintf(label, sizeof(label), "%s##google", str("link_connect_google"));
                if (ui_button(ui, label, x, y, bw, 44.0f, true)) {
                    *action = MENU_GOOGLE;
                }
            }
            if (acct->apple_ready) {
                SDL_snprintf(label, sizeof(label), "%s##apple", str("link_connect_apple"));
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
        y += 20.0f;
    }
    return y - y_start;
}

/* ------------------------------------------------------------------ panel */

/* In-game pause: a centred card, at most 420 pt wide. */
/* Right column of the pause card: my lobby, friends, who is here */
static void lobby_column(SettingsMenu *m, Ui *ui, MenuAction *action, float x, float y, float w, float h)
{
    Lobby *l = m->lobby;
    const NetSession *se = l->session;
    const float row = 44.0f, gap = 6.0f, y_end = y + h;
    char label[64];
    if (lobby_is_guest(l)) {
        SDL_snprintf(label, sizeof(label), "%s##p_leave", str("lobby_leave"));
        if (ui_button(ui, label, x, y, w, row, false)) {
            *action = MENU_MAIN_MENU;
            m->open = false;
            m->card = false;
        }
    } else if (lobby_can_toggle(l)) {
        SDL_snprintf(label, sizeof(label), "%s: %s##p_lobby", str("lobby_row"), str(l->open ? "lobby_open" : "lobby_locked"));
        bool open = l->open;
        ui_toggle(ui, label, x, y + 4.0f, w, &open);
        if (open != l->open) {
            lobby_set_open(l, open);
            *action = MENU_LOBBY_TOGGLE;
        }
    } else {
        SDL_snprintf(label, sizeof(label), "%s: %s", str("lobby_row"), str("lobby_offline"));
        ui_round_rect(ui->gfx, ui->font, x, y, w, row, 10.0f, ui_rgba(0xFFFFFF14));
        text_draw_centered(ui->gfx, ui->font, x + w * 0.5f, y + row * 0.5f, 16.0f, ui_rgba(0xFFFFFF80), label);
    }
    y += row + gap;
    SDL_snprintf(label, sizeof(label), "%s##p_friends", str("friends"));
    if (ui_button(ui, label, x, y, w, row, false)) {
        *action = MENU_FRIENDS;
    }
    if (m->friends->incoming > 0) {
        char n[8];
        SDL_snprintf(n, sizeof(n), "%d", m->friends->incoming);
        ui_circle(ui->gfx, ui->font, x + w - 24.0f, y + row * 0.5f, 12.0f, ui_rgba(0xE5484DFF));
        text_draw_centered(ui->gfx, ui->font, x + w - 24.0f, y + row * 0.5f, 13.0f, ui_rgba(0xFFFFFFFF), n);
    }
    y += row + gap + 2.0f;
    SDL_snprintf(label, sizeof(label), "%s (%d/%d)", str("lobby_players"), session_player_count(se), LOBBY_MAX_PLAYERS);
    ui_label(ui, x, y, 13.0f, k_ui_theme.text_dim, label);
    y += 20.0f;
    const float view = y_end - y, line = 44.0f, head = 40.0f;
    const float off = ui_scroll_begin(ui, &m->players_scroll, x, y, w, view, false);
    float py = y - off;
    head_icon_draw(m->heads, ui->gfx, ui->font, m->my_skin, m->my_name, x + head * 0.5f, py + line * 0.5f, head, false);
    text_draw(ui->gfx, ui->font, x + head + 8.0f, py + 12.0f, 15.0f, k_ui_theme.text, m->my_name);
    py += line;
    for (int i = 0; i < SESSION_MAX_REMOTE; i++) {
        const RemotePlayer *r = &se->remotes[i];
        if (!r->active) {
            continue;
        }
        head_icon_draw(m->heads, ui->gfx, ui->font, &r->meta.skin, r->meta.name, x + head * 0.5f, py + line * 0.5f, head, false);
        text_draw(ui->gfx, ui->font, x + head + 8.0f, py + 12.0f, 15.0f, k_ui_theme.text, r->meta.name);
        if (lobby_can_toggle(l)) {
            SDL_snprintf(label, sizeof(label), "%s##p_kick%d", str("lobby_kick"), i);
            if (ui_button(ui, label, x + w - 84.0f, py + 2.0f, 84.0f, 40.0f, false)) {
                lobby_kick(l, r->id);
            }
        }
        py += line;
    }
    ui_scroll_end(ui, &m->players_scroll, view, py + off - y);
}

static MenuAction pause_card(SettingsMenu *m, Ui *ui, GameSettings *s, float w_pt, float h_pt, float safe_t, float safe_b)
{
    MenuAction action = MENU_NONE;
    const float row = 50.0f, pad = 18.0f, gutter = 16.0f;
    const int rows = m->is_guest ? 5 : 4;
    const float room = h_pt - safe_t - safe_b - 12.0f;
    const float wanted = pad * 2.0f + 40.0f + (float)rows * (row + 8.0f);
    const float cw = mg_minf(680.0f, w_pt - 48.0f), ch = mg_minf(wanted, room); /* the column scrolls if it does not fit */
    const float x = (w_pt - cw) * 0.5f, top = safe_t + (h_pt - safe_t - safe_b - ch) * 0.5f;
    ui_block(ui, 0, 0, w_pt, h_pt);
    ui_rect(ui->gfx, ui->font, 0, 0, w_pt, h_pt, ui_rgba(0x00000080));
    ui_panel(ui, x, top, cw, ch);
    text_draw_centered(ui->gfx, ui->font, x + cw * 0.5f, top + pad + 14.0f, 24.0f, k_ui_theme.text, str("pause_title"));
    const float column = (cw - 2.0f * pad - gutter) * 0.5f;
    float y = top + pad + 40.0f;
    const float bx = x + pad, bw = column;
    char label[64];
    if (m->lobby) {
        lobby_column(m, ui, &action, bx + column + gutter, y, column, ch - pad * 2.0f - 40.0f);
    }
    const float column_top = y, column_view = ch - pad * 2.0f - 40.0f;
    const float column_off = ui_scroll_begin(ui, &m->card_scroll, bx - 2.0f, column_top, bw + 4.0f, column_view, false);
    y -= column_off;
    SDL_snprintf(label, sizeof(label), "%s##p_resume", str("resume"));
    if (ui_button(ui, label, bx, y, bw, row, true)) {
        m->open = false;
        m->card = false;
    }
    y += row + 8.0f;
    SDL_snprintf(label, sizeof(label), "%s##p_settings", str("settings_row"));
    if (ui_button(ui, label, bx, y, bw, row, false)) {
        m->card = false;
        m->from_card = true;
    }
    y += row + 8.0f;
    SDL_snprintf(label, sizeof(label), "%s##p_perf", str("performance_row"));
    ui_toggle(ui, label, bx, y + 7.0f, bw, &s->show_stats);
    y += row + 8.0f;
    SDL_snprintf(label, sizeof(label), "%s##p_home", str("save_and_menu"));
    if (ui_button(ui, label, bx, y, bw, row, false)) {
        action = MENU_MAIN_MENU;
        m->open = false;
        m->card = false;
    }
    y += row + 8.0f;
    if (m->is_guest) {
        SDL_snprintf(label, sizeof(label), "%s##p_link", str("link_row"));
        if (ui_button(ui, label, bx, y, bw, row, false)) {
            action = MENU_LINK_ACCOUNT;
            m->open = false;
            m->card = false;
        }
        y += row + 8.0f;
    }
    ui_scroll_end(ui, &m->card_scroll, column_view, y + column_off - column_top);
    return action;
}

MenuAction settings_menu(SettingsMenu *m, Ui *ui, GameSettings *s, const SettingsAccount *acct,
                         float w_pt, float h_pt, float safe_l, float safe_t, float safe_r, float safe_b,
                         const char *status)
{
    MenuAction action = MENU_NONE;
    m->preview_visible = false;
    bool held;
    if (!m->open) { /* pause button, top-left: part of the edge-to-edge HUD */
        const ui_frame_t uf = {w_pt, h_pt, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        float gs, gx, gy;
        ui_pause_rect(&uf, &gx, &gy, &gs);
        if (ui_hit_area(ui, "gear", gx - 6.0f, gy - 6.0f, gs + 12.0f, gs + 12.0f, &held)) {
            m->open = true;
            m->card = true;
            m->from_card = false;
            SDL_snprintf(m->name, sizeof(m->name), "%s", acct->name);
        }
        gfx_ui_blur_panel(ui->gfx, gx, gy, gs, gs, gs * 0.5f, HMM_V4(0.12f, 0.13f, 0.19f, held ? 0.8f : 0.4f));
        const float bar_w = 4.0f, bar_h = 15.0f, bar_gap = 5.0f, bar_r = 1.5f, bar_top = gy + (gs - bar_h) * 0.5f;
        ui_round_rect(ui->gfx, ui->font, gx + gs * 0.5f - bar_gap * 0.5f - bar_w, bar_top, bar_w, bar_h, bar_r, ui_rgba(0xFFFFFFFF));
        ui_round_rect(ui->gfx, ui->font, gx + gs * 0.5f + bar_gap * 0.5f, bar_top, bar_w, bar_h, bar_r, ui_rgba(0xFFFFFFFF));
        m->editing_name = false;
        if (!m->open) {
            return MENU_NONE;
        }
    }
    if (!m->editing_name && strcmp(m->name, acct->name) != 0 && action == MENU_NONE) {
        /* keep in sync with the account unless the player is typing */
        SDL_snprintf(m->name, sizeof(m->name), "%s", acct->name);
    }

    if (m->card) {
        return pause_card(m, ui, s, w_pt, h_pt, safe_t, safe_b);
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
        if (m->from_card) {
            m->card = true;
            m->from_card = false;
        } else {
            m->open = false;
        }
    }
    ui_circle(ui->gfx, ui->font, x0 + 22.0f, y0 + 22.0f, 22.0f, held ? ui_rgba(0xFFFFFF40) : ui_rgba(0xFFFFFF1A));
    ui_line(ui->gfx, ui->font, x0 + 26.0f, y0 + 12.0f, x0 + 16.0f, y0 + 22.0f, 3.5f, ui_rgba(0xFFFFFFFF));
    ui_line(ui->gfx, ui->font, x0 + 16.0f, y0 + 22.0f, x0 + 26.0f, y0 + 32.0f, 3.5f, ui_rgba(0xFFFFFFFF));
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
        float off = ui_scroll_begin(ui, &m->skin_scroll, x - 8.0f, y - 4.0f, w + 16.0f, h + 8.0f, false);
        float used = skin_tab(m, ui, acct, x, y - off, w, &action);
        ui_scroll_end(ui, &m->skin_scroll, h, used);
    }
    if (!m->open) {
        m->editing_name = false;
    }
    return action;
}
