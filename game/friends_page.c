#include "friends_page.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "strings.h"
#include "ui/ui_widgets.h"

#define ROW_H 56.0f
#define HEAD_ROW 40.0f
#define CODE_CHARS "23456789ABCDEFGHJKMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz"
#define SEARCH_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _"
#define SEARCH_WAIT_S 0.4

/* The string for a server error code, or the generic one */
static const char *err_text(const char *prefix, const char *code)
{
    char key[64];
    SDL_snprintf(key, sizeof(key), "%s_%s", prefix, code);
    const char *s = str(key);
    if (s == key) {
        SDL_snprintf(key, sizeof(key), "%s_generic", prefix);
        return str(strcmp(prefix, "friend_err") == 0 ? "friend_err_generic" : "join_err_generic");
    }
    return s;
}

void friends_page_open(FriendsPage *p, Friends *f)
{
    p->open = true;
    p->invite_mode = false;
    p->confirm_id[0] = 0;
    p->action_id[0] = 0;
    p->note[0] = 0;
    f->error[0] = 0;
    friends_page(f, true);
}

bool friends_page_editing(const FriendsPage *p) { return p->open && p->editing_search; }

static bool small_button(Ui *ui, const char *text, const char *id, float x, float y, float w, bool hi)
{
    char label[96];
    SDL_snprintf(label, sizeof(label), "%s##%s", text, id);
    return ui_button_v(ui, label, x, y, w, 46.0f, hi ? UI_V_ACCENT : UI_V_SECONDARY, UI_F_SMALL);
}

static void disabled_chip(Ui *ui, const char *text, float x, float y, float w)
{
    ui_button_v(ui, text, x, y, w, 46.0f, UI_V_DISABLED, UI_F_SMALL); /* not a target */
}

/* A menu row that does nothing yet, and says so */
static void soon_row(Ui *ui, const char *text, float x, float y, float w)
{
    ui_button_v(ui, text, x, y, w, 46.0f, UI_V_DISABLED, UI_F_SMALL);
    const char *tag = str("friend_soon");
    const float tw = ui_text_w(ui, TEXT_FACE_BOLD, tag, 11.0f) + 14.0f;
    ui_shape(ui->gfx, &(UiShape){.x = x + w - tw - 10.0f, .y = y + 11.0f, .w = tw, .h = 20.0f, .radius = -1.0f, .color = ui_rgba(k_ui.primary)});
    ui_text_c(ui, TEXT_FACE_BOLD, x + w - tw * 0.5f - 10.0f, y + 21.0f, 11.0f, ui_rgba(k_ui.ink), tag);
}

static bool in_my_lobby(const Lobby *l, const char *id)
{
    const NetSession *s = l->session;
    for (int i = 0; i < s->member_count; i++) {
        if (strcmp(s->members[i], id) == 0) {
            return true;
        }
    }
    return false;
}

/* Alone in their own menu: the only ones an invite reaches */
static bool can_invite(const Friends *f, int i)
{
    return !f->stale && f->status[i] != NET_FS_OFFLINE && f->in_menu[i] && f->players[i] <= 1 && f->status[i] != NET_FS_IN_OTHER_LOBBY;
}

static float row_text(Ui *ui, const char *name, const char *sub, float x, float y, bool faded)
{
    ui_text(ui, TEXT_FACE_BOLD, x, y + 7.0f, 17.0f, faded ? ui_rgba(k_ui.ink_soft) : ui_rgba(k_ui.ink), name);
    if (sub) {
        ui_text(ui, TEXT_FACE_BODY, x, y + 29.0f, 13.0f, ui_rgba(k_ui.ink_soft), sub);
    }
    return y;
}

static const char *status_text(const Friends *f, int i)
{
    static const char *const keys[] = {"friend_st_offline", "friend_st_menu", "friend_st_open", "friend_st_locked", "friend_st_full", "friend_st_other"};
    return str(keys[f->status[i]]);
}

static float tab_friends(FriendsPage *p, Ui *ui, Friends *f, Lobby *l, HeadIcons *h, float x, float y, float w, float head)
{
    const float y0 = y;
    int shown = 0;
    char id[32];
    const int passes = p->invite_mode ? 2 : 1; /* invite mode: whoever can be invited first */
    for (int pass = 0; pass < passes; pass++) {
        for (int o = 0; o < f->count; o++) {
            const int i = f->order[o];
            if (f->relation[i] != NET_REL_FRIEND || (p->invite_mode && can_invite(f, i) != (pass == 0))) {
                continue;
            }
            shown++;
            const bool away = f->stale || f->status[i] == NET_FS_OFFLINE;
            const bool waiting = l->state == LOBBY_JOINING && strcmp(l->join_host, f->id[i]) == 0;
            SDL_snprintf(id, sizeof(id), "frow_%s", f->code[i]);
            ui_stagger(ui, shown - 1);
            ui_row(ui, id, x - 6.0f, y, w + 12.0f, ROW_H, false);
            head_icon_draw(h, ui->gfx, ui->font, &f->skin[i], f->name[i], x + head * 0.5f, y + ROW_H * 0.5f, head, away);
            /* grey offline, blue in the menu, green in a game */
            const uint32_t dot = away ? k_ui.status_off : f->in_menu[i] ? k_ui.status_menu : k_ui.status_game;
            ui_circle(ui->gfx, ui->font, x + head * 0.85f, y + ROW_H * 0.5f + head * 0.35f, 8.0f, ui_rgba(k_ui.surface));
            ui_circle(ui->gfx, ui->font, x + head * 0.85f, y + ROW_H * 0.5f + head * 0.35f, 6.0f, ui_rgba(dot));
            const char *sub = waiting ? str("friend_waiting") : status_text(f, i);
            if (p->invite_mode && !can_invite(f, i)) { /* why not */
                sub = str(away ? "invite_reason_offline" : f->in_menu[i] ? "invite_reason_busy" : "invite_reason_in_game");
            }
            row_text(ui, f->name[i], sub, x + head + 10.0f, y, away);
            float rx = x + w;
            const float bw = 96.0f;
            if (!f->stale && !p->invite_mode) {
                rx -= 44.0f;
                SDL_snprintf(id, sizeof(id), "fmore_%s", f->code[i]);
                if (ui_icon_button(ui, id, UI_ICON_MENU, rx, y + 4.0f, 46.0f, UI_V_SECONDARY)) {
                    SDL_snprintf(p->action_id, sizeof(p->action_id), "%s", f->id[i]);
                    SDL_snprintf(p->action_name, sizeof(p->action_name), "%s", f->name[i]);
                }
                rx -= 8.0f;
            }
            if (waiting) {
                if (small_button(ui, str("friend_cancel"), "fjoin_cancel", rx - bw, y + 6.0f, bw, false)) {
                    lobby_cancel_join(l);
                }
                rx -= bw + 8.0f;
            } else if (!away && !p->invite_mode && f->status[i] == NET_FS_OPEN) {
                SDL_snprintf(id, sizeof(id), "fjoin_%s", f->code[i]);
                if (small_button(ui, str("friend_join"), id, rx - bw, y + 6.0f, bw, true)) {
                    lobby_ask_join(l, f->id[i]);
                }
                rx -= bw + 8.0f;
            }
            if (!away && lobby_is_host(l) && !in_my_lobby(l, f->id[i])) {
                if (lobby_invite_sent(l, f->id[i])) {
                    disabled_chip(ui, str("friend_invite_sent"), rx - bw, y + 6.0f, bw);
                } else if (can_invite(f, i)) {
                    SDL_snprintf(id, sizeof(id), "finvite_%s", f->code[i]);
                    if (small_button(ui, str("friend_invite"), id, rx - bw, y + 6.0f, bw, true)) {
                        lobby_invite_send(l, f->id[i]);
                    }
                } else if (!waiting) {
                    disabled_chip(ui, str("friend_invite"), rx - bw, y + 6.0f, bw);
                }
            }
            ui_stagger_end(ui);
            y += ROW_H + 4.0f;
        }
    }
    if (shown == 0) {
        ui_text(ui, TEXT_FACE_BODY, x, y + 10.0f, 15.0f, ui_rgba(k_ui.ink_soft), str("friends_none"));
        y += 40.0f;
    }
    return y - y0;
}

static float tab_requests(Ui *ui, Friends *f, HeadIcons *h, float x, float y, float w, float head)
{
    const float y0 = y;
    int shown = 0;
    char id[32];
    for (int pass = 0; pass < 2; pass++) {
        const uint8_t want = pass == 0 ? NET_REL_INCOMING : NET_REL_OUTGOING;
        int in_group = 0;
        for (int i = 0; i < f->count; i++) {
            if (f->relation[i] != want) {
                continue;
            }
            if (in_group++ == 0) {
                ui_label_caps(ui, x, y, str(pass == 0 ? "friends_incoming" : "friends_outgoing"));
                y += 24.0f;
            }
            shown++;
            SDL_snprintf(id, sizeof(id), "frqrow_%s", f->code[i]);
            ui_stagger(ui, shown - 1);
            ui_row(ui, id, x - 6.0f, y, w + 12.0f, ROW_H, false);
            head_icon_draw(h, ui->gfx, ui->font, &f->skin[i], f->name[i], x + head * 0.5f, y + ROW_H * 0.5f, head, f->stale);
            row_text(ui, f->name[i], f->code[i], x + head + 10.0f, y, f->stale);
            if (!f->stale && pass == 0) {
                SDL_snprintf(id, sizeof(id), "frq_ok_%s", f->code[i]);
                if (small_button(ui, str("friend_accept"), id, x + w - 212.0f, y + 4.0f, 104.0f, true)) {
                    friends_respond(f, f->id[i], true);
                    ui_tier_at(ui, id, UI_T2); /* a friend added */
                }
                SDL_snprintf(id, sizeof(id), "frq_no_%s", f->code[i]);
                if (small_button(ui, str("friend_decline"), id, x + w - 100.0f, y + 6.0f, 100.0f, false)) {
                    friends_respond(f, f->id[i], false);
                }
            } else if (!f->stale) {
                SDL_snprintf(id, sizeof(id), "frq_x_%s", f->code[i]);
                if (small_button(ui, str("friend_cancel"), id, x + w - 110.0f, y + 6.0f, 110.0f, false)) {
                    friends_remove(f, f->id[i]);
                }
            }
            ui_stagger_end(ui);
            y += ROW_H + 4.0f;
        }
        y += in_group ? 8.0f : 0.0f;
    }
    if (shown == 0) {
        ui_text(ui, TEXT_FACE_BODY, x, y + 10.0f, 15.0f, ui_rgba(k_ui.ink_soft), str("friends_no_requests"));
        y += 40.0f;
    }
    return y - y0;
}

/* The size at which the text fits in `room`, at most `size`. */
static float fit_size(Ui *ui, const char *text, float size, float room)
{
    const float tw = text_width(ui->font, text, size);
    return tw > room ? size * room / tw : size;
}

static bool looks_like_code(const char *s)
{
    if (strlen(s) != 6) {
        return false;
    }
    for (; *s; s++) {
        if (!strchr(CODE_CHARS, *s)) {
            return false;
        }
    }
    return true;
}

/* The Add tab: one bar that takes a friend code or a name. Returns its height. */
static float friends_header(FriendsPage *p, Ui *ui, Friends *f, MgApp *app, float x, float y, float w)
{
    const float y0 = y;
    if (!f->signed_in) {
        ui_text(ui, TEXT_FACE_BODY, x, y, 15.0f, ui_rgba(k_ui.ink_soft), str("friends_offline_note"));
        return y + 30.0f - y0;
    }
    /* your own code, the one way to be found when the name is common */
    char code[48];
    SDL_snprintf(code, sizeof(code), "%s %s", str("friend_your_code"), f->my_code[0] ? f->my_code : "------");
    ui_text(ui, TEXT_FACE_DISPLAY, x, y - 2.0f, 20.0f, ui_rgba(k_ui.ink), code);
    ui_text(ui, TEXT_FACE_BODY, x, y + 24.0f, 13.0f, ui_rgba(k_ui.ink_soft), str("friend_code_hint"));
    if (f->my_code[0] && small_button(ui, str(app->time < p->copied_until ? "friend_copied" : "friend_copy"), "fcopy", x + w - 110.0f, y, 110.0f, false)) {
        SDL_SetClipboardText(f->my_code);
        p->copied_until = app->time + 1.5;
    }
    y += 54.0f;
    ui_label_caps(ui, x, y, str("friend_search_field"));
    y += 20.0f;
    const int ed = ui_text_field(ui, "fsearch", x, y, w, 44.0f, p->search_in, (int)sizeof(p->search_in), &p->editing_search, SEARCH_CHARS);
    if (ed == UI_EDIT_CHANGED) {
        p->search_dirty = true;
        p->search_at = app->time + SEARCH_WAIT_S;
        f->search_count = 0;
        f->searched = false;
    }
    if (ed == UI_EDIT_DONE && looks_like_code(p->search_in)) {
        friends_request_code(f, p->search_in);
        p->search_in[0] = 0;
        p->search_dirty = false;
        p->editing_search = false;
        f->search_count = 0;
        f->searched = false;
        p->note_bad = false;
        SDL_snprintf(p->note, sizeof(p->note), "%s", str("friend_sent"));
        p->note_until = app->time + 2.5;
    }
    if (p->search_dirty && app->time >= p->search_at && strlen(p->search_in) >= 2) {
        p->search_dirty = false;
        friends_search(f, p->search_in);
    }
    return y + 54.0f - y0;
}

static void search_clear(FriendsPage *p, Friends *f)
{
    p->search_in[0] = 0;
    p->search_dirty = false;
    p->editing_search = false;
    f->search_count = 0;
    f->searched = false;
}

static float search_results(FriendsPage *p, Ui *ui, Friends *f, HeadIcons *h, MgApp *app, float x, float y, float w, float k)
{
    const float y0 = y;
    char id[32];
    const bool code = looks_like_code(p->search_in);
    if (code) { /* typed a friend code: it can be sent as it is */
        row_text(ui, p->search_in, str("friend_code_row"), x, y, false);
        if (ui_icon_button(ui, "fcode_add", UI_ICON_PLUS, x + w - 46.0f, y + 4.0f, 46.0f, UI_V_ACCENT)) {
            friends_request_code(f, p->search_in);
            p->note_bad = false;
        SDL_snprintf(p->note, sizeof(p->note), "%s", str("friend_sent"));
            p->note_until = app->time + 2.5;
            search_clear(p, f);
            return ROW_H;
        }
        y += ROW_H + 4.0f;
    }
    for (int i = 0; i < f->search_count; i++) {
        const NetFriendRow *r = &f->search[i];
        head_icon_draw(h, ui->gfx, ui->font, &r->skin, r->name, x + HEAD_ROW * k * 0.5f, y + ROW_H * 0.5f, HEAD_ROW * k, false);
        row_text(ui, r->name, r->code, x + HEAD_ROW * k + 10.0f, y, false);
        SDL_snprintf(id, sizeof(id), "fres_%s", r->code);
        if (ui_icon_button(ui, id, UI_ICON_PLUS, x + w - 46.0f, y + 4.0f, 46.0f, UI_V_ACCENT)) {
            friends_request_code(f, r->code);
            p->note_bad = false;
        SDL_snprintf(p->note, sizeof(p->note), "%s", str("friend_sent"));
            p->note_until = app->time + 2.5;
            search_clear(p, f);
            return ROW_H;
        }
        y += ROW_H + 4.0f;
    }
    if (f->searched && f->search_count == 0 && !code) {
        ui_text(ui, TEXT_FACE_BODY, x, y + 6.0f, 15.0f, ui_rgba(k_ui.ink_soft), str("friend_no_results"));
        y += 36.0f;
    }
    return y - y0;
}

bool friends_page_draw(FriendsPage *p, Ui *ui, Friends *f, Lobby *l, HeadIcons *h, MgApp *app, const char *my_name,
                       const NetSkin *my_skin)
{
    const float pad = 16.0f, w_pt = app->width_pt, h_pt = app->height_pt;
    const float k = h_pt >= 600.0f ? 1.3f : 1.0f;
    ui_block(ui, 0, 0, w_pt, h_pt);
    ui->modal = true;
    ui_rect(ui->gfx, ui->font, 0.0f, 0.0f, w_pt, h_pt, ui_rgba(k_ui.surface_tint)); /* flat, no blur */
    const float x0 = app->safe_left + pad, y0 = mg_maxf(app->safe_top, 8.0f) + 8.0f;
    const float inner_w = w_pt - app->safe_left - app->safe_right - 2.0f * pad;
    const float bottom = h_pt - mg_maxf(app->safe_bottom, 12.0f);
    /* as wide as the name needs, up to the middle of the screen */
    const float lw = mg_clampf(102.0f + text_width(ui->font, my_name, 20.0f) + 8.0f, 160.0f, w_pt * 0.5f - pad - x0);
    bool held, closed = false;
    if (ui_icon_button(ui, "back##friends", UI_ICON_BACK, x0, y0, k_ui.icon_button, UI_V_SECONDARY)) {
        closed = true;
    }
    (void)held;
    head_icon_draw(h, ui->gfx, ui->font, my_skin, my_name, x0 + 82.0f, y0 + 24.0f, 40.0f, false);
    const float name_size = fit_size(ui, my_name, 22.0f, lw - 110.0f);
    ui_text(ui, TEXT_FACE_DISPLAY, x0 + 110.0f, y0 + 24.0f - name_size * 0.62f, name_size, ui_rgba(k_ui.ink), my_name);
    float y = y0 + 64.0f;
    static const char *const tabs[3] = {"friends_tab_friends", "friends_tab_requests", "friends_tab_add"};
    for (int t = 0; t < 3; t++) {
        char label[64];
        SDL_snprintf(label, sizeof(label), "%s##ftab%d", str(tabs[t]), t);
        if (ui_button(ui, label, x0, y, lw, 50.0f, p->tab == t)) {
            p->tab = t;
            search_clear(p, f);
        }
        if (t == 1 && f->incoming > 0) {
            char n[8];
            SDL_snprintf(n, sizeof(n), "%d", f->incoming);
            ui_circle(ui->gfx, ui->font, x0 + lw - 22.0f, y + 22.0f, 12.0f, ui_rgba(k_ui.bad));
            text_draw_centered(ui->gfx, ui->font, x0 + lw - 22.0f, y + 22.0f, 13.0f, ui_rgba(k_ui.white), n);
        }
        y += 58.0f;
    }
    if (f->stale) {
        ui_label(ui, x0, y + 6.0f, 13.0f, k_ui_theme.text_dim, str("friends_offline_note"));
    }

    ui_rect(ui->gfx, ui->font, x0 + lw + pad, y0, 2.0f, bottom - y0, ui_rgba(k_ui.lip_mint)); /* account and tabs | content */
    const float x = x0 + lw + 2.0f * pad, w = x0 + inner_w - x;
    y = y0 + 4.0f;
    ui_text(ui, TEXT_FACE_DISPLAY, x, y, 28.0f, ui_rgba(k_ui.ink), str(tabs[p->tab]));
    if (p->invite_mode) { /* how much room is left */
        LobbyRow rows[LOBBY_MAX_PLAYERS];
        char line[32];
        SDL_snprintf(line, sizeof(line), "%s %d/%d", str("lobby_title"), lobby_roster(l, rows), LOBBY_MAX_PLAYERS);
        const float tw = text_width(ui->font, line, 15.0f);
        ui_text(ui, TEXT_FACE_BOLD, x + w - tw, y + 9.0f, 15.0f, ui_rgba(k_ui.ink_soft), line);
    }
    y += 40.0f;
    if (p->tab == 2) {
        y += friends_header(p, ui, f, app, x, y, w);
    }
    const float view = bottom - y;
    const float head = HEAD_ROW * k;
    const float off = ui_scroll_begin(ui, &p->scroll[p->tab], x - 8.0f, y - 4.0f, w + 16.0f, view + 8.0f, false);
    float used = 0.0f;
    if (p->tab == 0) {
        used = tab_friends(p, ui, f, l, h, x, y - off, w, head);
    } else if (p->tab == 1) {
        used = tab_requests(ui, f, h, x, y - off, w, head);
    } else if (strlen(p->search_in) >= 2) {
        used = search_results(p, ui, f, h, app, x, y - off, w, k);
    } else {
        ui_text(ui, TEXT_FACE_BODY, x, y - off + 10.0f, 15.0f, ui_rgba(k_ui.ink_soft), str("friend_add_hint"));
        used = 40.0f;
    }
    ui_scroll_end(ui, &p->scroll[p->tab], view, used);
    if (f->error[0]) { /* a refused call: its reason for a moment */
        SDL_snprintf(p->note, sizeof(p->note), "%s", err_text("friend_err", f->error));
        p->note_until = app->time + 3.0;
        p->note_bad = true;
        f->error[0] = 0;
        if (ui->fx) {
            ui_fx_tier(ui->fx, UI_T2_FAIL, 0.0f, 0.0f, false); /* a refused request */
        }
        ui_shake(ui, "fnote", 10.0f);
    }
    if (p->note[0] && app->time < p->note_until) {
        const float tw = ui_text_w(ui, TEXT_FACE_BOLD, p->note, 14.0f), sx = ui_shake_x(ui, "fnote");
        ui_surface(ui, x + (w - tw) * 0.5f - 14.0f + sx, bottom - 40.0f, tw + 28.0f, 32.0f, -1.0f, k_ui.lip_pill, ui_rgba(p->note_bad ? k_ui.bad : k_ui.ink),
                   ui_rgba(p->note_bad ? k_ui.bad_lip : k_ui.ink_lip), 0.0f, ui_rgba(0));
        ui_text_c(ui, TEXT_FACE_BOLD, x + w * 0.5f + sx, bottom - 24.0f, 14.0f, ui_rgba(k_ui.white), p->note);
    }
    if (p->action_id[0]) {
        const float dw = 300.0f, step = 54.0f, dh = 56.0f + 5.0f * step + 4.0f;
        float dx, dy;
        ui_modal(ui, "faction", dw, mg_minf(dh, h_pt - 20.0f), &dx, &dy);
        ui_text_c(ui, TEXT_FACE_DISPLAY, dx + dw * 0.5f, dy + 30.0f, 22.0f, ui_rgba(k_ui.ink), p->action_name);
        const float ax = dx + 16.0f, aw = dw - 32.0f;
        float ay = dy + 52.0f;
        char rl[64];
        SDL_snprintf(rl, sizeof(rl), "%s##fa_remove", str("friend_remove"));
        if (ui_button_v(ui, rl, ax, ay, aw, 46.0f, UI_V_DESTRUCTIVE, UI_F_SMALL)) {
            SDL_snprintf(p->confirm_id, sizeof(p->confirm_id), "%s", p->action_id);
            SDL_snprintf(p->confirm_name, sizeof(p->confirm_name), "%s", p->action_name);
            p->action_id[0] = 0;
        }
        ay += step;
        soon_row(ui, str("friend_block"), ax, ay, aw);
        ay += step;
        soon_row(ui, str("friend_report"), ax, ay, aw);
        ay += step;
        int who = -1;
        for (int i = 0; i < f->count; i++) {
            who = strcmp(f->id[i], p->action_id) == 0 ? i : who;
        }
        if (who >= 0 && lobby_is_host(l) && !lobby_invite_sent(l, p->action_id) && can_invite(f, who)) {
            if (small_button(ui, str("friend_invite"), "fa_invite", ax, ay, aw, true)) {
                lobby_invite_send(l, p->action_id);
                p->action_id[0] = 0;
            }
        } else {
            disabled_chip(ui, str(who >= 0 && lobby_invite_sent(l, p->action_id) ? "friend_invite_sent" : "friend_invite"), ax, ay - 6.0f, aw);
        }
        ay += step;
        if (small_button(ui, str("friend_cancel"), "fa_cancel", ax, ay, aw, false)) {
            p->action_id[0] = 0;
        }
        ui_modal_end(ui);
    }
    if (p->confirm_id[0]) {
        const float dw = 360.0f, dh = 160.0f;
        float dx, dy;
        ui_modal(ui, "fconfirm", dw, dh, &dx, &dy);
        ui_text_c(ui, TEXT_FACE_DISPLAY, dx + dw * 0.5f, dy + 30.0f, 22.0f, ui_rgba(k_ui.ink), str("friend_remove_q"));
        ui_text_c(ui, TEXT_FACE_BODY, dx + dw * 0.5f, dy + 60.0f, 15.0f, ui_rgba(k_ui.ink_soft), p->confirm_name);
        const float bw = (dw - 48.0f) * 0.5f;
        if (small_button(ui, str("friend_cancel"), "frm_no", dx + 16.0f, dy + dh - 62.0f, bw, false)) {
            p->confirm_id[0] = 0;
        }
        char yl[64];
        SDL_snprintf(yl, sizeof(yl), "%s##frm_yes", str("friend_remove"));
        if (ui_button_v(ui, yl, dx + 32.0f + bw, dy + dh - 62.0f, bw, 46.0f, UI_V_DESTRUCTIVE, UI_F_SMALL)) {
            friends_remove(f, p->confirm_id);
            p->confirm_id[0] = 0;
        }
        ui_modal_end(ui);
    }
    if (closed) {
        p->open = false;
        p->editing_search = false;
        friends_page(f, false);
    }
    return closed;
}
