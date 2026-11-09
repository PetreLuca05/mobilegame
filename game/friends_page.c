#include "friends_page.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "strings.h"

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
    return ui_button(ui, label, x, y, w, 44.0f, hi);
}

static float row_text(Ui *ui, const char *name, const char *sub, float x, float y, bool faded)
{
    text_draw(ui->gfx, ui->font, x, y + 8.0f, 17.0f, faded ? ui_rgba(0xFFFFFF80) : ui_rgba(0xFFFFFFFF), name);
    if (sub) {
        text_draw(ui->gfx, ui->font, x, y + 30.0f, 13.0f, ui_rgba(0xFFFFFF80), sub);
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
    for (int o = 0; o < f->count; o++) {
        const int i = f->order[o];
        if (f->relation[i] != NET_REL_FRIEND) {
            continue;
        }
        shown++;
        const bool waiting = l->state == LOBBY_JOINING && strcmp(l->join_host, f->id[i]) == 0;
        const bool away = f->stale || f->status[i] == NET_FS_OFFLINE;
        head_icon_draw(h, ui->gfx, ui->font, &f->skin[i], f->name[i], x + head * 0.5f, y + ROW_H * 0.5f, head, away);
        /* grey offline, blue in the menu, green in a game */
        const uint32_t dot = away ? 0x8A8F98FF : f->status[i] == NET_FS_MENU ? 0x3B82F6FF : 0x34C759FF;
        ui_circle(ui->gfx, ui->font, x + head * 0.85f, y + ROW_H * 0.5f + head * 0.35f, 8.0f, ui_rgba(0x14171FFF));
        ui_circle(ui->gfx, ui->font, x + head * 0.85f, y + ROW_H * 0.5f + head * 0.35f, 6.0f, ui_rgba(dot));
        row_text(ui, f->name[i], waiting ? str("friend_waiting") : status_text(f, i), x + head + 10.0f, y, away);
        float rx = x + w;
        if (!f->stale) {
            rx -= 44.0f;
            SDL_snprintf(id, sizeof(id), "fmore_%s", f->code[i]);
            if (small_button(ui, "", id, rx, y + 6.0f, 44.0f, false)) {
                SDL_snprintf(p->action_id, sizeof(p->action_id), "%s", f->id[i]);
                SDL_snprintf(p->action_name, sizeof(p->action_name), "%s", f->name[i]);
            }
            for (int bar = 0; bar < 3; bar++) { /* the menu icon */
                ui_round_rect(ui->gfx, ui->font, rx + 13.0f, y + 21.0f + (float)bar * 6.0f, 18.0f, 2.5f, 1.25f, ui_rgba(0xFFFFFFFF));
            }
            rx -= 8.0f;
        }
        const float bw = 110.0f;
        if (waiting) {
            if (small_button(ui, str("friend_cancel"), "fjoin_cancel", rx - bw, y + 6.0f, bw, false)) {
                lobby_cancel_join(l);
            }
        } else if (!away && f->status[i] == NET_FS_OPEN) {
            SDL_snprintf(id, sizeof(id), "fjoin_%s", f->code[i]);
            if (small_button(ui, str("friend_join"), id, rx - bw, y + 6.0f, bw, true)) {
                lobby_ask_join(l, f->id[i]);
            }
        } else if (!away) {
            SDL_snprintf(id, sizeof(id), "finvite_%s", f->code[i]);
            small_button(ui, str("friend_invite"), id, rx - bw, y + 6.0f, bw, true); /* no invites on the server yet */
        }
        y += ROW_H + 4.0f;
    }
    if (shown == 0) {
        text_draw(ui->gfx, ui->font, x, y + 10.0f, 15.0f, ui_rgba(0xFFFFFF99), str("friends_none"));
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
                text_draw(ui->gfx, ui->font, x, y, 13.0f, ui_rgba(0xFFFFFF80), str(pass == 0 ? "friends_incoming" : "friends_outgoing"));
                y += 24.0f;
            }
            shown++;
            head_icon_draw(h, ui->gfx, ui->font, &f->skin[i], f->name[i], x + head * 0.5f, y + ROW_H * 0.5f, head, f->stale);
            row_text(ui, f->name[i], f->code[i], x + head + 10.0f, y, f->stale);
            if (!f->stale && pass == 0) {
                SDL_snprintf(id, sizeof(id), "frq_ok_%s", f->code[i]);
                if (small_button(ui, str("friend_accept"), id, x + w - 212.0f, y + 6.0f, 104.0f, true)) {
                    friends_respond(f, f->id[i], true);
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
            y += ROW_H + 4.0f;
        }
        y += in_group ? 8.0f : 0.0f;
    }
    if (shown == 0) {
        text_draw(ui->gfx, ui->font, x, y + 10.0f, 15.0f, ui_rgba(0xFFFFFF99), str("friends_no_requests"));
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
        text_draw(ui->gfx, ui->font, x, y, 15.0f, ui_rgba(0xFFFFFF99), str("friends_offline_note"));
        return y + 30.0f - y0;
    }
    text_draw(ui->gfx, ui->font, x, y, 13.0f, ui_rgba(0xFFFFFF80), str("friend_search_field"));
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
        if (small_button(ui, "+", "fcode_add", x + w - 44.0f, y + 6.0f, 44.0f, true)) {
            friends_request_code(f, p->search_in);
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
        if (small_button(ui, "+", id, x + w - 44.0f, y + 6.0f, 44.0f, true)) {
            friends_request_code(f, r->code);
            SDL_snprintf(p->note, sizeof(p->note), "%s", str("friend_sent"));
            p->note_until = app->time + 2.5;
            search_clear(p, f);
            return ROW_H;
        }
        y += ROW_H + 4.0f;
    }
    if (f->searched && f->search_count == 0 && !code) {
        text_draw(ui->gfx, ui->font, x, y + 6.0f, 15.0f, ui_rgba(0xFFFFFF99), str("friend_no_results"));
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
    gfx_ui_blur_panel(ui->gfx, 0.0f, 0.0f, w_pt, h_pt, 0.0f, HMM_V4(0.05f, 0.06f, 0.09f, 0.82f));
    const float x0 = app->safe_left + pad, y0 = mg_maxf(app->safe_top, 8.0f) + 8.0f;
    const float inner_w = w_pt - app->safe_left - app->safe_right - 2.0f * pad;
    const float bottom = h_pt - mg_maxf(app->safe_bottom, 12.0f);
    /* as wide as the name needs, up to the middle of the screen */
    const float lw = mg_clampf(102.0f + text_width(ui->font, my_name, 20.0f) + 8.0f, 160.0f, w_pt * 0.5f - pad - x0);
    bool held, closed = false;
    if (ui_hit_area(ui, "back##friends", x0 - 6.0f, y0 - 6.0f, 56.0f, 56.0f, &held)) {
        closed = true;
    }
    ui_circle(ui->gfx, ui->font, x0 + 22.0f, y0 + 22.0f, 22.0f, held ? ui_rgba(0xFFFFFF40) : ui_rgba(0xFFFFFF1A));
    ui_line(ui->gfx, ui->font, x0 + 26.0f, y0 + 12.0f, x0 + 16.0f, y0 + 22.0f, 3.5f, ui_rgba(0xFFFFFFFF));
    ui_line(ui->gfx, ui->font, x0 + 16.0f, y0 + 22.0f, x0 + 26.0f, y0 + 32.0f, 3.5f, ui_rgba(0xFFFFFFFF));
    head_icon_draw(h, ui->gfx, ui->font, my_skin, my_name, x0 + 76.0f, y0 + 22.0f, 36.0f, false);
    const float name_size = fit_size(ui, my_name, 20.0f, lw - 102.0f);
    text_draw(ui->gfx, ui->font, x0 + 102.0f, y0 + 22.0f - name_size * 0.65f, name_size, ui_rgba(0xFFFFFFFF), my_name);
    float y = y0 + 64.0f;
    static const char *const tabs[3] = {"friends_tab_friends", "friends_tab_requests", "friends_tab_add"};
    for (int t = 0; t < 3; t++) {
        char label[64];
        SDL_snprintf(label, sizeof(label), "%s##ftab%d", str(tabs[t]), t);
        if (ui_button(ui, label, x0, y, lw, 44.0f, p->tab == t)) {
            p->tab = t;
            search_clear(p, f);
        }
        if (t == 1 && f->incoming > 0) {
            char n[8];
            SDL_snprintf(n, sizeof(n), "%d", f->incoming);
            ui_circle(ui->gfx, ui->font, x0 + lw - 22.0f, y + 22.0f, 12.0f, ui_rgba(0xE5484DFF));
            text_draw_centered(ui->gfx, ui->font, x0 + lw - 22.0f, y + 22.0f, 13.0f, ui_rgba(0xFFFFFFFF), n);
        }
        y += 54.0f;
    }
    if (f->stale) {
        ui_label(ui, x0, y + 6.0f, 13.0f, k_ui_theme.text_dim, str("friends_offline_note"));
    }

    ui_rect(ui->gfx, ui->font, x0 + lw + pad, y0, 1.0f, bottom - y0, ui_rgba(0xFFFFFF33)); /* account and tabs | content */
    const float x = x0 + lw + 2.0f * pad, w = x0 + inner_w - x;
    y = y0 + 4.0f;
    text_draw(ui->gfx, ui->font, x, y + 4.0f, 22.0f, ui_rgba(0xFFFFFFFF), str(tabs[p->tab]));
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
        text_draw(ui->gfx, ui->font, x, y - off + 10.0f, 15.0f, ui_rgba(0xFFFFFF99), str("friend_add_hint"));
        used = 40.0f;
    }
    ui_scroll_end(ui, &p->scroll[p->tab], view, used);
    if (f->error[0]) { /* a refused call: its reason for a moment */
        SDL_snprintf(p->note, sizeof(p->note), "%s", err_text("friend_err", f->error));
        p->note_until = app->time + 3.0;
        f->error[0] = 0;
    }
    if (p->note[0] && app->time < p->note_until) {
        const float tw = text_width(ui->font, p->note, 14.0f);
        ui_round_rect(ui->gfx, ui->font, x + (w - tw) * 0.5f - 12.0f, bottom - 36.0f, tw + 24.0f, 30.0f, 15.0f, ui_rgba(0x1E2230F0));
        text_draw_centered(ui->gfx, ui->font, x + w * 0.5f, bottom - 21.0f, 14.0f, ui_rgba(0xFFFFFFFF), p->note);
    }
    if (p->action_id[0]) {
        ui_block(ui, 0, 0, w_pt, h_pt);
        ui_rect(ui->gfx, ui->font, 0, 0, w_pt, h_pt, ui_rgba(0x00000080));
        const float dw = 300.0f, step = 52.0f, dh = 52.0f + 5.0f * step + 4.0f, dx = (w_pt - dw) * 0.5f, dy = (h_pt - dh) * 0.5f;
        ui_panel(ui, dx, dy, dw, dh);
        text_draw_centered(ui->gfx, ui->font, dx + dw * 0.5f, dy + 28.0f, 18.0f, ui_rgba(0xFFFFFFFF), p->action_name);
        const float ax = dx + 16.0f, aw = dw - 32.0f;
        float ay = dy + 52.0f;
        if (small_button(ui, str("friend_remove"), "fa_remove", ax, ay, aw, false)) {
            SDL_snprintf(p->confirm_id, sizeof(p->confirm_id), "%s", p->action_id);
            SDL_snprintf(p->confirm_name, sizeof(p->confirm_name), "%s", p->action_name);
            p->action_id[0] = 0;
        }
        ay += step;
        small_button(ui, str("friend_block"), "fa_block", ax, ay, aw, false); /* block, report, invite: not wired up yet */
        ay += step;
        small_button(ui, str("friend_report"), "fa_report", ax, ay, aw, false);
        ay += step;
        small_button(ui, str("friend_invite"), "fa_invite", ax, ay, aw, false);
        ay += step;
        if (small_button(ui, str("friend_cancel"), "fa_cancel", ax, ay, aw, false)) {
            p->action_id[0] = 0;
        }
    }
    if (p->confirm_id[0]) {
        ui_block(ui, 0, 0, w_pt, h_pt);
        ui_rect(ui->gfx, ui->font, 0, 0, w_pt, h_pt, ui_rgba(0x00000080));
        const float dw = 360.0f, dh = 150.0f, dx = (w_pt - dw) * 0.5f, dy = (h_pt - dh) * 0.5f;
        ui_panel(ui, dx, dy, dw, dh);
        text_draw_centered(ui->gfx, ui->font, dx + dw * 0.5f, dy + 30.0f, 18.0f, ui_rgba(0xFFFFFFFF), str("friend_remove_q"));
        text_draw_centered(ui->gfx, ui->font, dx + dw * 0.5f, dy + 58.0f, 15.0f, ui_rgba(0xFFFFFFB3), p->confirm_name);
        const float bw = (dw - 48.0f) * 0.5f;
        if (small_button(ui, str("friend_cancel"), "frm_no", dx + 16.0f, dy + dh - 62.0f, bw, false)) {
            p->confirm_id[0] = 0;
        }
        if (small_button(ui, str("friend_remove"), "frm_yes", dx + 32.0f + bw, dy + dh - 62.0f, bw, true)) {
            friends_remove(f, p->confirm_id);
            p->confirm_id[0] = 0;
        }
    }
    if (closed) {
        p->open = false;
        p->editing_search = false;
        friends_page(f, false);
    }
    return closed;
}
