#include "ui/layout.h"
#include "settings_menu.h"
#include "ui/haptics.h"

#include <SDL3/SDL_stdinc.h>
#include <string.h>

#include "avatar.h"
#include "strings.h"
#include "ui/text_check.h"
#include "ui/ui_widgets.h"

#define NAME_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-."

/* A widget label: the text of a string-table key + "##id" (the id stays put whatever the text says), in the
 * function's `lb`: every widget call reads its label before the next L(). */
static const char *label_of(char *buf, size_t cap, const char *key, const char *id)
{
    SDL_snprintf(buf, cap, "%s##%s", str(key), id);
    return buf;
}
#define L(key, id) label_of(lb, sizeof(lb), (key), (id))


/* ------------------------------------------------------------------ game */

static float game_tab(SettingsMenu *m, Ui *ui, GameSettings *s, float x, float y, float w, MenuAction *action)
{
    const float y_top = y;
    const float step = 52.0f;
    char lb[128];
    bool wide = w > 560.0f;
    float cw = wide ? (w - 24.0f) * 0.5f : w;
    float x2 = x + cw + 24.0f;
#ifdef MG_DAYCYCLE_CONTINUOUS
    /* Day / night: drag to pick an hour (stops following the clock) */
    int hh = (int)s->time_of_day, mm = (int)((s->time_of_day - (float)hh) * 60.0f);
    char tod[64];
    SDL_snprintf(tod, sizeof(tod), "%s  %02d:%02d##tod", str("set_day_night"), hh, mm);
    if (ui_slider(ui, tod, x, y, w, &s->time_of_day, 0.0f, 23.99f, "")) {
        *action = MENU_TIME;
    }
    y += 56.0f;
    ui_toggle(ui, L("set_follow_clock", "clock"), x, y, cw, &s->follow_clock);
    static const float speeds[3] = {1.0f, 60.0f, 600.0f};
    static const char *const speed_labels[3] = {"x1##spd", "x60##spd", "x600##spd"};
    float sx = wide ? x2 : x, sy = wide ? y : y + 44.0f;
    float sw = (cw - 16.0f) / 3.0f;
    for (int i = 0; i < 3; i++) {
        if (ui_button(ui, speed_labels[i], sx + (float)i * (sw + 8.0f), sy - 2.0f, sw, k_ui.target_min,
                      s->time_speed == speeds[i])) {
            s->time_speed = speeds[i];
        }
    }
    y = sy + 52.0f;
    ui_rect(ui->gfx, ui->font, x, y - 6.0f, w, 1.0f, ui_rgba(k_ui.hairline));
    y += 6.0f;
#endif
    float cx = x, cy = y;
    const float sliders_top = y;
#ifndef MG_CAMERA_THIRDPERSON
    ui_toggle(ui, str("show_stats"), cx, cy, cw, &s->show_stats);
    cy += 52.0f;
    ui_label_caps(ui, cx, cy + 4.0f, str("chat_bubbles"));
    cy += 22.0f;
    static const char *const bubble_keys[3] = {"npc_off##bub", "bubble_active##bub", "npc_all##bub"};
    const float bub_w = (cw - 16.0f) / 3.0f;
    for (int i = 0; i < 3; i++) {
        if (ui_button(ui, str(bubble_keys[i]), cx + (float)i * (bub_w + 8.0f), cy, bub_w, k_ui.target_min, s->chat_bubbles == i)) {
            s->chat_bubbles = i;
        }
    }
    cy += 52.0f;
    ui_label_caps(ui, cx, cy + 4.0f, str("npc_density"));
    cy += 22.0f;
    static const float npc_d[3] = {0.0f, 0.5f, 1.0f};
    static const char *const npc_d_keys[3] = {"npc_off##den", "npc_low##den", "npc_normal##den"};
    const float third = (cw - 16.0f) / 3.0f;
    for (int i = 0; i < 3; i++) {
        if (ui_button(ui, str(npc_d_keys[i]), cx + (float)i * (third + 8.0f), cy, third, k_ui.target_min, s->npc_density == npc_d[i])) {
            s->npc_density = npc_d[i];
        }
    }
    cy += 52.0f;
    ui_toggle(ui, L("set_show_own_name", "own_name"), cx, cy, cw, &s->show_own_name);
    cy += 52.0f;
    ui_toggle(ui, L("set_show_xray", "xray"), cx, cy, cw, &s->show_xray);
    cy += 52.0f;
    if (ui_haptic_supported()) { /* HAPTIC-1 Vibrații: hidden without a Taptic Engine */
        ui_label_caps(ui, cx, cy + 4.0f, str("haptics"));
        cy += 22.0f;
        static const char *const keys[3] = {"haptics_off", "haptics_fine", "haptics_full"};
        const float vib_w = (cw - 16.0f) / 3.0f;
        for (int i = 0; i < 3; i++) {
            char hl[64];
            SDL_snprintf(hl, sizeof(hl), "%s##vib%d", str(keys[i]), i);
            if (ui_button(ui, hl, cx + (float)i * (vib_w + 8.0f), cy, vib_w, k_ui.target_min, s->haptics == i) && s->haptics != i) {
                s->haptics = i;
                ui_haptic_set_level((HapticLevel)i);
                ui_haptic(HAPTIC_PRIMARY); /* a preview at the new level */
            }
        }
        cy += 56.0f;
    }
    { /* TILT-1 Efect 3D la înclinare: Oprit / Fin / Complet */
        ui_label_caps(ui, cx, cy + 4.0f, str("ui_tilt"));
        cy += 22.0f;
        static const char *const tkeys[3] = {"haptics_off", "haptics_fine", "haptics_full"};
        const float tw3 = (cw - 16.0f) / 3.0f;
        for (int i = 0; i < 3; i++) {
            char tl[64];
            SDL_snprintf(tl, sizeof(tl), "%s##tilt%d", str(tkeys[i]), i);
            if (ui_button(ui, tl, cx + (float)i * (tw3 + 8.0f), cy, tw3, k_ui.target_min, s->ui_tilt == i)) {
                s->ui_tilt = i;
            }
        }
        cy += 56.0f;
    }
#else
    ui_slider(ui, L("set_cam_yaw_sens", "s1"), cx, cy, cw, &s->yaw_deg_per_pt, 0.05f, 0.6f, "%.2f" TEXT_DEGREE "/pt");
    cy += step;
    ui_slider(ui, L("set_cam_pitch_sens", "s2"), cx, cy, cw, &s->pitch_deg_per_pt, 0.05f, 0.5f, "%.2f" TEXT_DEGREE "/pt");
    cy += step;
    if (ui_slider(ui, L("set_cam_pitch", "s4"), cx, cy, cw, &s->cam_pitch_deg, -60.0f, 30.0f, "%.0f" TEXT_DEGREE)) {
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
    ui_slider(ui, L("set_fov", "s5"), cx, cy, cw, &s->fov_deg, 45.0f, 75.0f, "%.0f" TEXT_DEGREE);
    cy += step;
#endif
    ui_slider(ui, L("set_dead_zone", "s6"), cx, cy, cw, &s->dead_zone_pt, 0.0f, 50.0f, "%.0f pt");
    cy += step + 4.0f;
    ui_toggle(ui, L("set_sound", "sound"), cx, cy, cw, &s->sound_on);
    cy += 52.0f;
    /* the interface rows sit in the second column (the first is full on a phone: the last row reached the home indicator) */
    ui_toggle(ui, str("ui_sounds"), cx, cy, cw, &s->ui_sounds);
    cy += 52.0f;
    ui_label_caps(ui, cx, cy + 4.0f, str("ui_motion")); /* UI-1: Animații interfață: Complete / Reduse */
    cy += 22.0f;
    {
        static const char *const motion_keys[2] = {"ui_motion_full", "ui_motion_reduced"};
        const float half = (cw - 8.0f) * 0.5f;
        for (int i = 0; i < 2; i++) {
            char ml[64];
            SDL_snprintf(ml, sizeof(ml), "%s##mot%d", str(motion_keys[i]), i);
            if (ui_button(ui, ml, cx + (float)i * (half + 8.0f), cy, half, 46.0f, s->ui_motion == i)) {
                s->ui_motion = i;
            }
        }
    }
    cy += 58.0f;
    if (m->screen_hz > 61.0f) { /* ProMotion / VRR screens: Rată de cadre: Maximă (the screen's rate) / 60 (battery and heat) */
        ui_label_caps(ui, cx, cy + 4.0f, str("frame_rate"));
        cy += 22.0f;
        const float half = (cw - 8.0f) * 0.5f;
        for (int i = 0; i < 2; i++) {
            char fl[64], txt[32];
            if (i == 0) {
                SDL_snprintf(txt, sizeof(txt), "%s (%.0f)", str("frame_rate_max"), (double)m->screen_hz);
            } else {
                SDL_snprintf(txt, sizeof(txt), "%s", str("frame_rate_60"));
            }
            SDL_snprintf(fl, sizeof(fl), "%s##fps%d", txt, i);
            if (ui_button(ui, fl, cx + (float)i * (half + 8.0f), cy, half, 46.0f, s->frame_rate == i)) {
                s->frame_rate = i;
            }
        }
        cy += 58.0f;
    }
#ifdef MG_CAMERA_THIRDPERSON
    ui_toggle(ui, L("set_invert_y", "invert_y"), cx, cy, cw, &s->invert_y);
    cy += 52.0f;
#endif
    y = mg_maxf(cy, left_end) + 6.0f;


    char dev[64];
    SDL_snprintf(dev, sizeof(dev), "%s %s##dev", str("set_developer"), m->dev_open ? "-" : "+");
    if (ui_button(ui, dev, x, y, cw, 44.0f, m->dev_open)) {
        m->dev_open = !m->dev_open;
    }
    y += 56.0f;
    if (m->dev_open) {
#ifndef NDEBUG
        if (ui_button(ui, L("set_haptic_lab", "hlab"), x, y, cw, k_ui.target_min, false)) {
            m->lab_open = true;
        }
        y += 56.0f;
#endif
        ui_toggle(ui, L("dev_colliders", "colliders"), x, y, cw, &s->show_colliders);
        ui_toggle(ui, L("dev_triggers", "triggers"), wide ? x2 : x, wide ? y : y + 52.0f, cw, &s->show_triggers);
        y += wide ? 52.0f : 104.0f;
        ui_toggle(ui, L("dev_nav_grid", "nav"), x, y + (wide ? 52.0f : 104.0f), cw, &s->show_nav);
        ui_toggle(ui, L("dev_storeys", "storeys"), x, y, cw, &s->show_storeys);
        ui_toggle(ui, L("dev_xray_always", "xray_always"), wide ? x2 : x, wide ? y : y + 52.0f, cw, &s->xray_always);
        y += (wide ? 52.0f : 104.0f) + 52.0f;
#ifndef NDEBUG
        ui_toggle(ui, L("dev_tilt_debug", "tilt_debug"), x, y, cw, &s->show_tilt_debug); /* TILT-1: gravity, mapping, arrows */
        y += 52.0f;
#endif
#ifdef MG_RENDER_FULL
        ui_toggle(ui, L("dev_light_gizmos", "gizmos"), x, y, cw, &s->show_lights);
        y += 52.0f;
        /* Lighting */
        static const char *const tiers[3] = {"dev_tier_auto", "dev_tier_low", "dev_tier_high"};
        float tb = (cw - 16.0f) / 3.0f;
        for (int i = 0; i < 3; i++) {
            if (ui_button(ui, L(tiers[i], tiers[i]), x + (float)i * (tb + 8.0f), y, tb, k_ui.target_min, s->tier_override == i)) {
                s->tier_override = i;
            }
        }
        y += 52.0f;
        ui_toggle(ui, L("dev_shadows", "shadows"), x, y, cw, &s->shadows_on);
        y += 52.0f;
        ui_toggle(ui, L("dev_cascades", "cascades"), x, y, cw, &s->show_cascades);
        y += 52.0f;
        ui_toggle(ui, L("dev_freeze_shadows", "freeze_shadows"), x, y, cw, &s->freeze_shadows);
        y += 52.0f;
#endif
        ui_toggle(ui, L("dev_perf", "dev_perf"), x, y, cw, &s->show_stats);
        y += 52.0f;
        ui_toggle(ui, L("dev_force_lift", "force_lift"), x, y, cw, &s->force_lift_panel);
        y += 52.0f;
        ui_toggle(ui, L("dev_ranges", "ranges"), x, y, cw, &s->show_ia_ranges);
        y += 52.0f;
        ui_toggle(ui, L("dev_active_id", "active_id"), x, y, cw, &s->show_ia_active);
        y += 52.0f;
        ui_toggle(ui, L("dev_preview_any", "preview_any"), x, y, cw, &s->preview_any);
        y += 52.0f;
        ui_toggle(ui, L("dev_safe_rect", "safe_rect"), x, y, cw, &s->show_safe_rect);
        y += 52.0f;
        ui_toggle(ui, L("dev_profiler", "profiler"), x, y, cw, &s->show_profiler);
        y += 52.0f;
        ui_toggle(ui, L("dev_slots", "slots"), x, y, cw, &s->show_slots);
        y += 52.0f;
        ui_toggle(ui, L("dev_npc_packages", "packages"), x, y, cw, &s->show_packages);
        y += 52.0f;
        ui_toggle(ui, L("dev_npc_paths", "npc_paths"), x, y, cw, &s->show_npc_paths);
        y += 52.0f;
        ui_toggle(ui, L("dev_freeze_npcs", "freeze_npcs"), x, y, cw, &s->freeze_npcs);
        y += 52.0f;
        if (ui_button(ui, L("dev_respawn_npcs", "npc"), x, y, cw, k_ui.target_min, false)) {
            s->respawn_npcs = true;
        }
        y += 52.0f;
        ui_label_caps(ui, x, y + 4.0f, str("dev_stress"));
        y += 22.0f;
        ui_slider(ui, L("dev_dummies", "stress"), x, y, cw, &s->stress_count, 0.0f, 400.0f, "%.0f");
        y += step;
        ui_label_caps(ui, x, y + 4.0f, str("dev_markers"));
        y += 22.0f;
        ui_toggle(ui, L("dev_test_markers", "test_markers"), x, y, cw, &s->test_markers);
        y += 52.0f;
        ui_slider(ui, L("dev_marker_count", "mkc"), x, y, cw, &s->test_marker_count, 1.0f, 16.0f, "%.0f");
        y += step;
        ui_toggle(ui, L("dev_all_paths", "all_paths"), x, y, cw, &s->show_all_paths);
        y += 52.0f;
        if (ui_button(ui, L("dev_respawn_markers", "mkr"), x, y, cw, k_ui.target_min, false)) {
            *action = MENU_RESPAWN_MARKERS;
        }
        y += 52.0f;
        ui_label_caps(ui, x, y + 4.0f, str("dev_teleport"));
        y += 22.0f;
        const int nf = m->tp_floors < 1 ? 1 : (m->tp_floors > 8 ? 8 : m->tp_floors);
        float tw = (cw - 8.0f * (float)(nf - 1)) / (float)nf;
        for (int i = 0; i < nf; i++) {
            if (ui_button(ui, m->tp_label[i][0] ? m->tp_label[i] : "G##tp", x + (float)i * (tw + 8.0f), y, tw, 44.0f, false)) {
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
    char lb[128];
    m->preview_visible = false;
    text_check_data(acct->name); /* names data: the account's display name (the line under the button) */
    ui_label_caps(ui, x, y, str("username_title"));
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
            ui_button(ui, "…##google", x, y, cw, 44.0f, false);
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
    } else if (ui_button(ui, L("sign_out", "sign_out"), x, y, cw, 44.0f, false)) {
        *action = MENU_SIGN_OUT;
    }
    y += 52.0f;
    if (acct->can_erase) { /* what New Game was: destructive, behind a confirmation */
        SDL_snprintf(label, sizeof(label), "%s##erase_progress", str("erase_progress"));
        if (ui_button_v(ui, label, x, y, cw, 44.0f, UI_V_DESTRUCTIVE, UI_F_SMALL) && !m->confirm_erase) {
            m->confirm_erase = true;
        }
        y += 52.0f;
    }
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
        ui_button_v(ui, label, x, y, w, row, UI_V_DISABLED, UI_F_SMALL);
    }
    y += row + gap;
    SDL_snprintf(label, sizeof(label), "%s##p_friends", str("friends"));
    if (ui_button(ui, label, x, y, w, row, false)) {
        *action = MENU_FRIENDS;
    }
    if (m->friends->incoming > 0) {
        char n[8];
        SDL_snprintf(n, sizeof(n), "%d", m->friends->incoming);
        ui_circle(ui->gfx, ui->font, x + w - 24.0f, y + row * 0.5f, 12.0f, ui_rgba(k_ui.bad));
        text_draw_centered(ui->gfx, ui->font, x + w - 24.0f, y + row * 0.5f, 13.0f, ui_rgba(k_ui.white), n);
    }
    y += row + gap + 2.0f;
    SDL_snprintf(label, sizeof(label), "%s (%d/%d)", str("lobby_players"), session_player_count(se), LOBBY_MAX_PLAYERS);
    ui_label_caps(ui, x, y, label);
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
            if (ui_button_v(ui, label, x + w - 84.0f, py + 2.0f, 84.0f, k_ui.target_min, UI_V_DESTRUCTIVE, UI_F_SMALL)) { /* kick: destructive */
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
#ifdef MG_PROF
    const int rows = (m->is_guest ? 5 : 4) + 2 + (m->journal_row ? 1 : 0); /* + Map, + the test row, + Misiuni */
#else
    const int rows = (m->is_guest ? 5 : 4) + 1 + (m->journal_row ? 1 : 0);
#endif
    const float room = h_pt - safe_t - safe_b - 12.0f;
    const float wanted = pad * 2.0f + 40.0f + (float)rows * (row + 8.0f);
    const float cw = mg_minf(680.0f, w_pt - 48.0f), ch = mg_minf(wanted, room); /* the column scrolls if it does not fit */
    const float x = (w_pt - cw) * 0.5f, top = safe_t + (h_pt - safe_t - safe_b - ch) * 0.5f;
    ui_block(ui, 0, 0, w_pt, h_pt);
    ui_rect(ui->gfx, ui->font, 0, 0, w_pt, h_pt, ui_rgba(k_ui.scrim)); /* flat scrim: no blur */
    ui->modal = true;
    ui_card(ui, x, top, cw, ch);
    ui_text_c(ui, TEXT_FACE_DISPLAY, x + cw * 0.5f, top + pad + 14.0f, 28.0f, ui_rgba(k_ui.ink), str("pause_title"));
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
    if (ui_button_v(ui, label, bx, y, bw, row, UI_V_PRIMARY, UI_F_BREATHE)) { /* the main button of the card */
        m->open = false;
        m->card = false;
    }
    y += row + 8.0f;
    if (m->journal_row) { /* BOOK-ENGINE S4: the missions' journal */
        SDL_snprintf(label, sizeof(label), "%s##p_journal", str("journal_row"));
        if (ui_button(ui, label, bx, y, bw, row, false)) {
            action = MENU_JOURNAL; /* the page is drawn instead of the card; the menu stays open (the game is paused) */
        }
        y += row + 8.0f;
    }
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
    SDL_snprintf(label, sizeof(label), "%s##p_map", str("map_row"));
    if (ui_button(ui, label, bx, y, bw, row, false)) {
        action = MENU_OPEN_MAP;
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
#ifdef MG_PROF
    SDL_snprintf(label, sizeof(label), "%s##p_progress", str("debug_progress"));
    if (ui_button(ui, label, bx, y, bw, row, false)) {
        action = MENU_DEBUG_PROGRESS; /* the card stays up */
    }
    y += row + 8.0f;
#endif
    ui_scroll_end(ui, &m->card_scroll, column_view, y + column_off - column_top);
    return action;
}

#ifndef NDEBUG
/* HAPTIC-1 Haptic lab: every entry by name, rank and sharpness, a tap plays it; the second tab plays the ranks
 * and the composites. How the feel is reviewed on the iPhone. */
static void haptic_lab(SettingsMenu *m, Ui *ui, float w_pt, float h_pt)
{
    const float w = mg_minf(w_pt - 40.0f, 620.0f), h = h_pt - 40.0f;
    float x, y;
    ui_modal(ui, "hlab", w, h, &x, &y);
    text_check_exempt_push(); /* debug builds only (NDEBUG): haptic table names and numbers, never in a release */
    ui_text(ui, TEXT_FACE_DISPLAY, x + 20.0f, y + 14.0f, 22.0f, ui_rgba(k_ui.ink), "Haptic lab");
    if (ui_icon_button(ui, "hlab_close", UI_ICON_CLOSE, x + w - 64.0f, y + 10.0f, k_ui.icon_button, UI_V_SECONDARY)) {
        m->lab_open = false;
    }
    static const char *const tabs[2] = {"Entries", "Ranks + patterns"};
    ui_tabs(ui, "hlab_tab", tabs, 2, &m->lab_tab, x + 20.0f, y + 72.0f, w - 40.0f, k_ui.target_min);
    static const HapticId demo[] = {HAPTIC_DEMO_R1, HAPTIC_DEMO_R2, HAPTIC_DEMO_R3, HAPTIC_DEMO_R4, HAPTIC_DEMO_R5, HAPTIC_CORRECT,
                                    HAPTIC_WRONG, HAPTIC_WARNING_MODAL, HAPTIC_NOTICE, HAPTIC_MAIN, HAPTIC_COMPLETE};
    const int count = m->lab_tab == 0 ? HAPTIC_COUNT : (int)(sizeof(demo) / sizeof(demo[0]));
    const float top = y + 128.0f, view = h - 140.0f, row = 50.0f;
    const float off = ui_scroll_begin(ui, &m->lab_scroll, x + 12.0f, top, w - 24.0f, view, false);
    for (int i = 0; i < count; i++) {
        const HapticId id = m->lab_tab == 0 ? (HapticId)i : demo[i];
        const HapticEntry *e = &k_haptics[id];
        const float ry = top + (float)i * (row + 6.0f) - off;
        char rid[24], info[64];
        SDL_snprintf(rid, sizeof(rid), "hlab_%d", (int)id);
        ui->hclass = HC_NONE; /* the row plays only the entry under test */
        if (ui_row(ui, rid, x + 20.0f, ry, w - 40.0f, row, true)) {
            ui_haptic(id);
        }
        ui_text(ui, TEXT_FACE_BOLD, x + 36.0f, ry + 14.0f, k_ui.t_body, ui_rgba(k_ui.ink), e->name);
        SDL_snprintf(info, sizeof(info), "R%d  %.2f / %.1f  x%d", e->rank, (double)e->ev[0].intensity, (double)e->ev[0].sharpness, e->n);
        ui_text(ui, TEXT_FACE_BODY, x + w - 200.0f, ry + 15.0f, k_ui.t_small, ui_rgba(k_ui.ink_soft), info);
    }
    ui_scroll_end(ui, &m->lab_scroll, view, (float)count * (row + 6.0f));
    text_check_exempt_pop();
    ui_modal_end(ui);
}
#endif

MenuAction settings_menu(SettingsMenu *m, Ui *ui, GameSettings *s, const SettingsAccount *acct,
                         float w_pt, float h_pt, float safe_l, float safe_t, float safe_r, float safe_b,
                         const char *status)
{
    const bool lab = m->lab_open && m->open && !m->card;
    if (lab) {
        ui->input_off = true; /* the page under the lab takes no touches */
    }
    MenuAction action = MENU_NONE;
    m->preview_visible = false;
    bool held;
    if (!m->open) { /* pause button, top right: part of the edge-to-edge HUD */
        const ui_frame_t uf = ui_hud_frame(w_pt, h_pt, safe_l, safe_r, safe_t, safe_b, 0.0f); /* phones: the safe area */
        float gs, gx, gy;
        ui_pause_rect(&uf, &gx, &gy, &gs);
        if (ui_icon_button(ui, "gear", UI_ICON_PAUSE, gx, gy, gs, UI_V_ACCENT)) {
            m->open = true;
            m->card = true;
            m->from_card = false;
            SDL_snprintf(m->name, sizeof(m->name), "%s", acct->name);
        }
        (void)held;
        m->editing_name = false;
        if (!m->open) {
            return MENU_NONE;
        }
    }
    if (!m->editing_name && strcmp(m->name, acct->name) != 0 && action == MENU_NONE) {
        /* keep in sync with the account unless the player is typing */
        SDL_snprintf(m->name, sizeof(m->name), "%s", acct->name);
    }

    text_check_set_screen(m->card ? "PAUSE" : "SETTINGS"); /* the frame's screen was set before a tap opened this */
    if (m->card) {
        return pause_card(m, ui, s, w_pt, h_pt, safe_t, safe_b);
    }

    /* full screen, two columns: back + title + tabs left (35 %), content right (65 %) */
    const float pad = 16.0f;
    ui_block(ui, 0, 0, w_pt, h_pt); /* modal: nothing reaches the game */
    ui->modal = true;
    ui_rect(ui->gfx, ui->font, 0.0f, 0.0f, w_pt, h_pt, ui_rgba(k_ui.surface_tint)); /* flat, no blur */
    float x0 = safe_l + pad, y0 = mg_maxf(safe_t, 8.0f) + 8.0f;
    float inner_w = w_pt - safe_l - safe_r - 2.0f * pad;
    float bottom = h_pt - mg_maxf(safe_b, 12.0f);
    float lw = inner_w * 0.35f - pad;
    if (ui_icon_button(ui, "back##menu", UI_ICON_BACK, x0, y0, k_ui.icon_button, UI_V_SECONDARY)) {
        if (m->from_card) {
            m->card = true;
            m->from_card = false;
        } else {
            m->open = false;
        }
    }
    (void)held;
    ui_text(ui, TEXT_FACE_DISPLAY, x0 + k_ui.icon_button + 12.0f, y0 + 2.0f, mg_minf(k_ui.t_title, lw * 0.22f), ui_rgba(k_ui.ink), str("settings"));
    float y = y0 + 74.0f;
    const char *const tab_names[2] = {str("set_tab_game"), str("set_tab_account")};
    ui_tabs(ui, "tab", tab_names, 2, &m->tab, x0, y, lw, 50.0f);
    y += 66.0f;
    if (status && status[0]) {
        ui_text(ui, TEXT_FACE_BODY, x0, y, k_ui.t_small, ui_rgba(k_ui.ink_soft), status);
    }

    float x = x0 + inner_w * 0.35f + pad, w = inner_w * 0.65f - pad;
    y = y0 + 4.0f;
    float h = bottom - y;
    if (m->tab == 0) {
        m->editing_name = false;
        float off = ui_scroll_begin(ui, &m->game_scroll, x - 8.0f, y - 4.0f, w + 16.0f, h + 4.0f, false); /* ends at the safe bottom */
        float used = game_tab(m, ui, s, x, y - off, w, &action);
        ui_scroll_end(ui, &m->game_scroll, h, used);
    } else {
        float off = ui_scroll_begin(ui, &m->skin_scroll, x - 8.0f, y - 4.0f, w + 16.0f, h + 4.0f, false); /* ends at the safe bottom */
        float used = skin_tab(m, ui, acct, x, y - off, w, &action);
        ui_scroll_end(ui, &m->skin_scroll, h, used);
    }
    if (m->confirm_erase) { /* Erase progress: confirm */
        float dw = 360.0f, dh = 170.0f, dx, dy;
        ui_modal_warning(ui, "erase", dw, dh, &dx, &dy);
        ui_text_c(ui, TEXT_FACE_DISPLAY, dx + dw * 0.5f, dy + 40.0f, 22.0f, ui_rgba(k_ui.ink), str("erase_progress_q"));
        const float bw = (dw - 48.0f) * 0.5f;
        char c1[48], c2[48];
        SDL_snprintf(c1, sizeof(c1), "%s##erase_no", str("cancel"));
        SDL_snprintf(c2, sizeof(c2), "%s##erase_yes", str("erase_progress"));
        if (ui_button_v(ui, c1, dx + 16.0f, dy + dh - 66.0f, bw, 50.0f, UI_V_SECONDARY, UI_F_SMALL)) {
            m->confirm_erase = false;
        }
        if (ui_button_v(ui, c2, dx + 32.0f + bw, dy + dh - 66.0f, bw, 50.0f, UI_V_DESTRUCTIVE, UI_F_SMALL | UI_F_CONFIRM)) {
            m->confirm_erase = false;
            action = MENU_ERASE_PROGRESS;
        }
        ui_modal_end(ui);
    }
    if (!m->open) {
        m->editing_name = false;
        m->confirm_erase = false;
    }
#ifndef NDEBUG
    if (lab) {
        ui->input_off = false;
        haptic_lab(m, ui, w_pt, h_pt);
    }
#endif
    return action;
}
