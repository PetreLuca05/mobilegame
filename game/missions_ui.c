#include "missions_ui.h"

#include <SDL3/SDL.h>

#include "core/mg_types.h"
#include "strings.h"
#include "ui/ui_style.h"

#define U16_NONE 0xFFFFu

void missions_ui_init(MissionUi *u)
{
    SDL_zerop(u);
    ui_text_area_init(&u->dlg_area, u->dlg_lines, (int)SDL_arraysize(u->dlg_lines));
    u->pill_steps_shown = -1;
}

uint32_t missions_case_color(const Missions *m, int mission)
{
    const mgpk_mission_t *c = mission >= 0 && m->on ? book_mission(m->pack, (uint32_t)mission) : NULL;
    const int arc = c ? missions_arc_of_chapter(m->pack, c->chapter) : 0;
    return k_ui.chapter[(arc < 0 ? 0 : arc) % 6];
}

const char *missions_storey_name(int storey, char *buf, int cap)
{
    if (storey <= 0) {
        SDL_snprintf(buf, (size_t)cap, "%s", str("storey_ground"));
    } else if (storey >= 4) {
        SDL_snprintf(buf, (size_t)cap, "%s", str("storey_roof"));
    } else {
        SDL_snprintf(buf, (size_t)cap, "%s %d", str("storey_n"), storey);
    }
    return buf;
}

static const char *room_name(const Missions *m, int room)
{
    const mgpk_room_t *r = room >= 0 ? book_room(m->pack, (uint32_t)room) : NULL;
    return r ? book_pack_str(m->pack, r->name) : str("mission_corridor");
}

/* the largest size <= want at which s fits in w */
static float fit(const Ui *ui, int face, const char *s, float want, float w)
{
    const float tw = ui_text_w(ui, face, s, want);
    return tw <= w || tw <= 0.0f ? want : mg_maxf(want * w / tw, 9.0f);
}

/* ------------------------------------------------------------------ pill */

MissionUiAction missions_pill(MissionUi *u, Missions *m, Ui *ui, float x, float y, float max_w)
{
    u->pill[2] = 0.0f;
    if (!m->on) {
        return MUI_NONE;
    }
    const MissionObjective *o = &m->obj;
    char l1[160], l2[200], st[32], n[16];
    if (o->kind == MOBJ_NONE) {
        SDL_snprintf(l1, sizeof(l1), "%s", str("mission_none"));
        l2[0] = 0;
        n[0] = 0;
    } else {
        const mgpk_mission_t *c = book_mission(m->pack, (uint32_t)o->mission);
        SDL_snprintf(l1, sizeof(l1), "%s", c ? book_pack_str(m->pack, c->title) : "");
        const char *what = o->kind == MOBJ_OFFER ? str("mission_talk_to") : o->kind == MOBJ_DEBRIEF ? str("mission_report_to") : str("mission_go_to");
        SDL_snprintf(l2, sizeof(l2), "%s %s · %s · %s", what, missions_cast_name(m->pack, o->cast), room_name(m, o->room),
                     missions_storey_name(o->storey, st, sizeof(st)));
        SDL_snprintf(n, sizeof(n), "%d/%d", o->steps_done, o->steps_total);
    }
    const float h = 58.0f, pad = 12.0f, strip = 6.0f;
    const float nw = n[0] ? ui_text_w(ui, TEXT_FACE_DISPLAY, n, 18.0f) + 14.0f : 0.0f;
    const float want = mg_maxf(ui_text_w(ui, TEXT_FACE_BOLD, l1, 14.0f), ui_text_w(ui, TEXT_FACE_BODY, l2, 12.0f));
    const float w = mg_clampf(want + pad * 2.0f + strip + nw, 160.0f, max_w);
    const float tw = w - pad * 2.0f - strip - nw;
    bool held;
    const bool tapped = ui_press(ui, "##mpill", x, y, w, h, &held, false);
    ui_depth(ui, 1, ui_key("##mpill"), 0);
    ui_surface(ui, x, y, w, h - k_ui.lip_pill, 16.0f, k_ui.lip_pill, ui_rgba(k_ui.surface), ui_rgba(k_ui.lip_mint), 0.0f, ui_rgba(0));
    ui_round_rect(ui->gfx, ui->font, x + 6.0f, y + 8.0f, strip, h - k_ui.lip_pill - 16.0f, strip * 0.5f, ui_rgba(missions_case_color(m, o->mission)));
    const float tx = x + pad + strip;
    ui_text(ui, TEXT_FACE_BOLD, tx, y + 8.0f, fit(ui, TEXT_FACE_BOLD, l1, 14.0f, tw), ui_rgba(k_ui.ink), l1);
    if (l2[0]) {
        ui_text(ui, TEXT_FACE_BODY, tx, y + 29.0f, fit(ui, TEXT_FACE_BODY, l2, 12.0f, tw), ui_rgba(k_ui.ink_soft), l2);
    }
    if (n[0]) {
        ui_text_c(ui, TEXT_FACE_DISPLAY, x + w - pad - nw * 0.5f + 4.0f, y + (h - k_ui.lip_pill) * 0.5f, 18.0f, ui_rgba(k_ui.teal_text), n);
    }
    ui_depth_end(ui);
    u->pill[0] = x, u->pill[1] = y, u->pill[2] = w, u->pill[3] = h;
    return tapped ? MUI_OPEN_JOURNAL : MUI_NONE;
}

/* ------------------------------------------------------------------ dialogue card */

void missions_dialogue_card(MissionUi *u, Missions *m, Ui *ui, MgApp *app, HeadIcons *heads)
{
    u->card[2] = 0.0f;
    if (!missions_dialogue_open(m)) {
        return;
    }
    MissionDialogue *d = &m->dlg;
    const uint32_t row = d->lines[d->at];
    const int speaker = missions_line_speaker(m, row) >= 0 ? missions_line_speaker(m, row) : d->cast;
    const float W = app->width_pt, H = app->height_pt;
    const float k = H >= 600.0f ? 1.25f : 1.0f;
    const float sl = app->safe_left + 12.0f, sr = app->safe_right + 12.0f;
    const float w = mg_minf(W - sl - sr, 860.0f * k), h = mg_clampf(H * 0.32f, 168.0f, 236.0f);
    const float x = sl + (W - sl - sr - w) * 0.5f, y = H - mg_maxf(app->safe_bottom, 12.0f) - h;
    ui_block(ui, 0.0f, 0.0f, W, H); /* talking: the stick and the world take no touches */
    ui->modal = true;
    ui_rect(ui->gfx, ui->font, 0.0f, 0.0f, W, H, ui_rgba(k_ui.scrim));
    ui_depth(ui, 4, 0, 0);
    ui_card(ui, x, y, w, h);
    const float pad = 16.0f * k, size = mg_minf(h - 2.0f * pad - 54.0f * k, 92.0f * k);
    const float pcx = x + pad + size * 0.5f, pcy = y + pad + size * 0.5f;
    const NetSkin skin = missions_cast_skin(m->pack, speaker);
    const char *name = missions_cast_name(m->pack, speaker);
    ui_circle(ui->gfx, ui->font, pcx, pcy, size * 0.5f + 4.0f, ui_rgba(missions_case_color(m, d->mission)));
    head_icon_draw(heads, ui->gfx, ui->font, &skin, name, pcx, pcy, size, false);
    const float tx = x + pad * 2.0f + size, tw = x + w - pad - tx;
    ui_text(ui, TEXT_FACE_DISPLAY, tx, y + pad - 2.0f, fit(ui, TEXT_FACE_DISPLAY, name, 20.0f * k, tw * 0.6f), ui_rgba(k_ui.ink), name);
    const char *role = missions_cast_role(m->pack, speaker);
    const float nw = ui_text_w(ui, TEXT_FACE_DISPLAY, name, fit(ui, TEXT_FACE_DISPLAY, name, 20.0f * k, tw * 0.6f));
    ui_text(ui, TEXT_FACE_BODY, tx + nw + 10.0f, y + pad + 4.0f * k, fit(ui, TEXT_FACE_BODY, role, 13.0f * k, mg_maxf(tw - nw - 10.0f, 40.0f)),
            ui_rgba(k_ui.ink_soft), role);
    const float by = y + h - k_ui.lip_card - pad - 48.0f * k; /* buttons' top */
    const float ty = y + pad + 30.0f * k;
    ui_text_area(ui, &u->dlg_area, missions_line(m, row), TEXT_FACE_BODY, 16.0f * k, ui_rgba(k_ui.ink), tx, ty, tw, mg_maxf(by - ty - 6.0f, 20.0f));
    { /* where in the conversation */
        char at[16];
        SDL_snprintf(at, sizeof(at), "%d/%d", d->at + 1, d->count);
        ui_text(ui, TEXT_FACE_BOLD, x + pad, by + 14.0f * k, 13.0f * k, ui_rgba(k_ui.ink_soft), at);
    }
    const float bh = 48.0f * k;
    const bool last = d->at + 1 >= d->count;
    char label[96];
    if (last && d->choices) {
        const float bw = mg_minf(200.0f * k, (tw - 12.0f) * 0.5f);
        SDL_snprintf(label, sizeof(label), "%s##mdlg_no", str("dlg_later"));
        if (ui_button_v(ui, label, x + w - pad - bw * 2.0f - 12.0f, by, bw, bh, UI_V_SECONDARY, UI_F_SMALL)) {
            missions_dialogue_choose(m, false);
        }
        SDL_snprintf(label, sizeof(label), "%s##mdlg_yes", d->kind == MDLG_OFFER ? str("dlg_accept") : str("dlg_start"));
        if (ui_button_v(ui, label, x + w - pad - bw, by, bw, bh, UI_V_PRIMARY, UI_F_SMALL | UI_F_BREATHE | UI_F_MAIN)) {
            ui_tier_at(ui, "mdlg_yes", UI_T2);
            missions_dialogue_choose(m, true);
        }
    } else {
        const float bw = mg_minf(200.0f * k, tw);
        SDL_snprintf(label, sizeof(label), "%s##mdlg_next", str("dlg_continue"));
        if (ui_button_v(ui, label, x + w - pad - bw, by, bw, bh, UI_V_PRIMARY, UI_F_SMALL | UI_F_BREATHE)) {
            missions_dialogue_next(m);
        }
    }
    ui_depth_end(ui);
    u->card[0] = x, u->card[1] = y, u->card[2] = w, u->card[3] = h;
}

/* ------------------------------------------------------------------ journal */

static float wrapped_h(MissionUi *u, const Ui *ui, const char *s, float size, float w, int *n)
{
    const TextFont *f = ui_face(ui, TEXT_FACE_BODY);
    *n = SDL_min(text_wrap(f, s, SDL_strlen(s), size, w, u->wrap, MISSION_UI_WRAP), MISSION_UI_WRAP);
    return (float)*n * text_line_height(f, size);
}

static float case_card_h(MissionUi *u, const Missions *m, const Ui *ui, int c, float w)
{
    const mgpk_mission_t *cs = &m->pack->missions[c];
    int n;
    const float lh = wrapped_h(u, ui, missions_line(m, cs->case_line), 14.0f, w - 32.0f, &n);
    return 16.0f + 28.0f + lh + 10.0f + (float)cs->n_quests * 30.0f + 10.0f + 48.0f + 16.0f + k_ui.lip_card;
}

static MissionUiAction case_card(MissionUi *u, Missions *m, Ui *ui, int c, float x, float y, float w, float h)
{
    MissionUiAction act = MUI_NONE;
    const mgpk_mission_t *cs = &m->pack->missions[c];
    const bool closed = missions_case_closed(m, c), accepted = missions_case_accepted(m, c), done = missions_case_done(m, c);
    ui_card(ui, x, y, w, h - k_ui.lip_card);
    ui_round_rect(ui->gfx, ui->font, x + 12.0f, y + 14.0f, 6.0f, 22.0f, 3.0f, ui_rgba(missions_case_color(m, c)));
    const char *status = closed ? str("case_closed") : !accepted ? str("case_new") : done ? str("case_ready") : str("case_accepted");
    const float chip = ui_tag_chip_w(ui, status);
    const char *title = book_pack_str(m->pack, cs->title);
    ui_text(ui, TEXT_FACE_DISPLAY, x + 26.0f, y + 12.0f, fit(ui, TEXT_FACE_DISPLAY, title, 20.0f, w - 60.0f - chip), ui_rgba(k_ui.ink), title);
    ui_tag_chip(ui, status, x + w - 16.0f, y + 14.0f);
    float yy = y + 16.0f + 28.0f;
    { /* the one-line summary, wrapped (never cut) */
        const char *s = missions_line(m, cs->case_line);
        int n;
        wrapped_h(u, ui, s, 14.0f, w - 32.0f, &n);
        const TextFont *f = ui_face(ui, TEXT_FACE_BODY);
        for (int i = 0; i < n; i++) {
            text_draw_n(ui->gfx, f, x + 16.0f, yy, 14.0f, ui_rgba(k_ui.ink_soft), s + u->wrap[i].start, u->wrap[i].len);
            yy += text_line_height(f, 14.0f);
        }
    }
    yy += 10.0f;
    for (int q = 0; q < cs->n_quests; q++) { /* its stages: one row per quest */
        const int qi = cs->first_quest + q;
        const mgpk_quest_t *qq = book_quest(m->pack, (uint32_t)qi);
        const mgpk_topic_t *t = qq ? book_topic(m->pack, qq->topic) : NULL;
        int d, tot;
        missions_quest_count(m, qi, &d, &tot);
        char cnt[16], pid[24];
        SDL_snprintf(cnt, sizeof(cnt), "%d/%d", d, tot);
        SDL_snprintf(pid, sizeof(pid), "jq%d", qi);
        const char *tt = t ? book_pack_str(m->pack, t->title) : "";
        const float bar = mg_minf(110.0f, w * 0.22f);
        ui_circle(ui->gfx, ui->font, x + 22.0f, yy + 10.0f, 5.0f, ui_rgba(d == tot && tot > 0 ? k_ui.ok : k_ui.off));
        ui_text(ui, TEXT_FACE_BOLD, x + 34.0f, yy + 1.0f, fit(ui, TEXT_FACE_BOLD, tt, 14.0f, w - bar - 110.0f), ui_rgba(k_ui.ink), tt);
        ui_text(ui, TEXT_FACE_BOLD, x + w - 16.0f - bar - 44.0f, yy + 1.0f, 13.0f, ui_rgba(k_ui.teal_text), cnt);
        ui_progress(ui, pid, x + w - 16.0f - bar, yy + 3.0f, bar, 14.0f, tot > 0 ? (float)d / (float)tot : 0.0f);
        yy += 30.0f;
    }
    yy += 10.0f;
    const float bw = mg_minf(190.0f, (w - 44.0f) * 0.5f);
    char label[96];
    if (m->tracked == c) {
        SDL_snprintf(label, sizeof(label), "%s##jr_tracked%d", str("journal_tracked"), c);
        ui_button_v(ui, label, x + 16.0f, yy, bw, 48.0f, UI_V_DISABLED, UI_F_SMALL);
    } else {
        SDL_snprintf(label, sizeof(label), "%s##jr_track%d", str("journal_track"), c);
        if (ui_button_v(ui, label, x + 16.0f, yy, bw, 48.0f, closed ? UI_V_DISABLED : UI_V_SECONDARY, UI_F_SMALL) && !closed) {
            missions_track(m, c);
        }
    }
    SDL_snprintf(label, sizeof(label), "%s##jr_go%d", str("journal_take_me"), c);
    if (ui_button_v(ui, label, x + 28.0f + bw, yy, bw, 48.0f, closed ? UI_V_DISABLED : UI_V_PRIMARY, UI_F_SMALL) && !closed) {
        missions_track(m, c);
        act = MUI_TAKE_ME;
    }
    return act;
}

MissionUiAction missions_journal(MissionUi *u, Missions *m, Ui *ui, MgApp *app, HeadIcons *heads)
{
    (void)heads;
    MissionUiAction act = MUI_NONE;
    const float pad = 16.0f, W = app->width_pt, H = app->height_pt;
    ui_block(ui, 0, 0, W, H);
    ui->modal = true;
    ui_rect(ui->gfx, ui->font, 0.0f, 0.0f, W, H, ui_rgba(k_ui.surface_tint)); /* flat, no blur */
    const float x0 = app->safe_left + pad, y0 = mg_maxf(app->safe_top, 8.0f) + 8.0f;
    const float inner_w = W - app->safe_left - app->safe_right - 2.0f * pad;
    const float bottom = H - mg_maxf(app->safe_bottom, 12.0f);
    const float lw = mg_clampf(inner_w * 0.3f, 170.0f, 260.0f);
    if (ui_icon_button(ui, "back##journal", UI_ICON_BACK, x0, y0, k_ui.icon_button, UI_V_SECONDARY)) {
        act = MUI_CLOSE_JOURNAL;
    }
    ui_text(ui, TEXT_FACE_DISPLAY, x0 + k_ui.icon_button + 12.0f, y0 + 6.0f, fit(ui, TEXT_FACE_DISPLAY, str("journal_title"), 28.0f, lw - k_ui.icon_button - 12.0f),
            ui_rgba(k_ui.ink), str("journal_title"));
    if (!m->on || m->pack->arc_count == 0) {
        return act;
    }
    u->arc_tab = SDL_clamp(u->arc_tab, 0, (int)m->pack->arc_count - 1);
    { /* arc tabs, a column that scrolls when the chapters do not fit */
        const float ty = y0 + 70.0f, view = bottom - ty, row = 54.0f;
        const float off = ui_scroll_begin(ui, &u->tabs_scroll, x0 - 2.0f, ty, lw + 4.0f, view, false);
        for (uint32_t a = 0; a < m->pack->arc_count; a++) {
            char label[160];
            SDL_snprintf(label, sizeof(label), "%s##jarc%u", book_pack_str(m->pack, m->pack->arcs[a].title), a);
            const float by = ty + (float)a * row - off;
            if (ui_button(ui, label, x0, by, lw, 46.0f, u->arc_tab == (int)a)) {
                u->arc_tab = (int)a;
                u->scroll.offset = 0.0f;
            }
            ui_circle(ui->gfx, ui->font, x0 + 16.0f, by + 21.0f, 5.0f, ui_rgba(k_ui.chapter[a % 6]));
        }
        ui_scroll_end(ui, &u->tabs_scroll, view, (float)m->pack->arc_count * row);
    }
    ui_rect(ui->gfx, ui->font, x0 + lw + pad, y0, 2.0f, bottom - y0, ui_rgba(k_ui.lip_mint));
    const float x = x0 + lw + 2.0f * pad, w = x0 + inner_w - x;
    const mgpk_arc_t *arc = &m->pack->arcs[u->arc_tab];
    float y = y0 + 4.0f;
    const char *at = book_pack_str(m->pack, arc->title);
    ui_text(ui, TEXT_FACE_DISPLAY, x, y, fit(ui, TEXT_FACE_DISPLAY, at, 26.0f, w), ui_rgba(k_ui.ink), at);
    y += 44.0f;
    const float view = bottom - y;
    const float off = ui_scroll_begin(ui, &u->scroll, x - 8.0f, y - 4.0f, w + 16.0f, view + 8.0f, false);
    float used = 0.0f;
    int idx = 0;
    for (uint32_t c = 0; c < m->pack->mission_count; c++) {
        if (m->pack->missions[c].chapter != arc->chapter) {
            continue;
        }
        const float ch = case_card_h(u, m, ui, (int)c, w);
        ui_stagger(ui, idx++);
        const MissionUiAction a = case_card(u, m, ui, (int)c, x, y + used - off, w, ch);
        ui_stagger_end(ui);
        if (a != MUI_NONE) {
            act = a;
        }
        used += ch + 12.0f;
    }
    if (idx == 0) {
        ui_text(ui, TEXT_FACE_BODY, x, y + 8.0f, k_ui.t_body, ui_rgba(k_ui.ink_soft), str("journal_empty"));
    }
    ui_scroll_end(ui, &u->scroll, view, used);
    return act;
}
