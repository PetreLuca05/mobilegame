#include "ui_widgets.h"

#include <math.h>
#include <string.h>

#include <SDL3/SDL_stdinc.h>

static UiColor C(uint32_t c) { return ui_rgba(c); }

static UiColor with_alpha(UiColor c, float a)
{
    c.a = (uint8_t)((float)c.a * (a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a));
    return c;
}

static const TextFont *face_of(const Ui *ui, int face)
{
    if (face == TEXT_FACE_DISPLAY && ui->display) {
        return ui->display;
    }
    if (face == TEXT_FACE_BOLD && ui->bold) {
        return ui->bold;
    }
    return ui->font;
}

float ui_text(Ui *ui, int face, float x, float y, float size, UiColor c, const char *s) { return text_draw(ui->gfx, face_of(ui, face), x, y, size, c, s); }

void ui_text_c(Ui *ui, int face, float cx, float cy, float size, UiColor c, const char *s) { text_draw_centered(ui->gfx, face_of(ui, face), cx, cy, size, c, s); }

float ui_text_w(const Ui *ui, int face, const char *s, float size) { return text_width(face_of(ui, face), s, size); }

void ui_variant_colors(UiVariant v, UiColor *face, UiColor *lip, UiColor *text)
{
    *face = C(v == UI_V_PRIMARY ? k_ui.primary : v == UI_V_SECONDARY ? k_ui.surface : v == UI_V_ACCENT ? k_ui.accent : v == UI_V_DESTRUCTIVE ? k_ui.bad : v == UI_V_OK ? k_ui.ok : k_ui.disabled);
    *lip = C(v == UI_V_PRIMARY ? k_ui.primary_lip : v == UI_V_SECONDARY ? k_ui.secondary_lip : v == UI_V_ACCENT ? k_ui.accent_lip : v == UI_V_DESTRUCTIVE ? k_ui.bad_lip : v == UI_V_OK ? k_ui.ok_lip : k_ui.disabled_lip);
    *text = C(v == UI_V_PRIMARY || v == UI_V_SECONDARY ? k_ui.ink : v == UI_V_DISABLED ? k_ui.disabled_text : 0xFFFFFFFFu);
}

void ui_icon(Ui *ui, UiIcon icon, float cx, float cy, float size, UiColor c)
{
    const float s = size / k_ui.icon_box, w = k_ui.icon_stroke * s, x0 = cx - size * 0.5f, y0 = cy - size * 0.5f;
#define P(px, py) x0 + (px) * size, y0 + (py) * size
#define L(ax, ay, bx, by) ui_line(ui->gfx, ui->font, P(ax, ay), P(bx, by), w, c)
    switch (icon) {
    case UI_ICON_BACK:
    case UI_ICON_PREV:
        L(0.62f, 0.22f, 0.36f, 0.5f);
        L(0.36f, 0.5f, 0.62f, 0.78f);
        break;
    case UI_ICON_NEXT:
        L(0.38f, 0.22f, 0.64f, 0.5f);
        L(0.64f, 0.5f, 0.38f, 0.78f);
        break;
    case UI_ICON_CLOSE:
        L(0.28f, 0.28f, 0.72f, 0.72f);
        L(0.72f, 0.28f, 0.28f, 0.72f);
        break;
    case UI_ICON_CHECK:
        L(0.24f, 0.52f, 0.42f, 0.70f);
        L(0.42f, 0.70f, 0.78f, 0.30f);
        break;
    case UI_ICON_PLUS:
        L(0.5f, 0.24f, 0.5f, 0.76f);
        L(0.24f, 0.5f, 0.76f, 0.5f);
        break;
    case UI_ICON_MINUS:
        L(0.24f, 0.5f, 0.76f, 0.5f);
        break;
    case UI_ICON_PAUSE:
        L(0.38f, 0.28f, 0.38f, 0.72f);
        L(0.62f, 0.28f, 0.62f, 0.72f);
        break;
    case UI_ICON_MENU:
        L(0.24f, 0.30f, 0.76f, 0.30f);
        L(0.24f, 0.50f, 0.76f, 0.50f);
        L(0.24f, 0.70f, 0.76f, 0.70f);
        break;
    case UI_ICON_CROWN:
        L(0.20f, 0.72f, 0.80f, 0.72f);
        L(0.20f, 0.72f, 0.16f, 0.34f);
        L(0.16f, 0.34f, 0.36f, 0.52f);
        L(0.36f, 0.52f, 0.50f, 0.26f);
        L(0.50f, 0.26f, 0.64f, 0.52f);
        L(0.64f, 0.52f, 0.84f, 0.34f);
        L(0.84f, 0.34f, 0.80f, 0.72f);
        break;
    case UI_ICON_COPY:
        ui_shape(ui->gfx, &(UiShape){.x = x0 + 0.20f * size, .y = y0 + 0.32f * size, .w = 0.42f * size, .h = 0.48f * size, .radius = 3.0f * s, .stroke = w, .color = c});
        ui_shape(ui->gfx, &(UiShape){.x = x0 + 0.38f * size, .y = y0 + 0.18f * size, .w = 0.42f * size, .h = 0.48f * size, .radius = 3.0f * s, .stroke = w, .color = c});
        break;
    default:
        break;
    }
#undef L
#undef P
}

/* caps for the labels: ASCII and the Romanian letters (UTF-8 two-byte forms) */
static void to_caps(const char *s, char *out, size_t cap)
{
    size_t n = 0;
    while (*s && n + 3 < cap) {
        const unsigned char c0 = (unsigned char)s[0], c1 = (unsigned char)s[1];
        if (c0 == 0xC4 && c1 == 0x83) { /* ă -> Ă */
            out[n++] = (char)0xC4, out[n++] = (char)0x82, s += 2;
        } else if (c0 == 0xC3 && (c1 == 0xA2 || c1 == 0xAE)) { /* â î */
            out[n++] = (char)0xC3, out[n++] = (char)(c1 - 0x20), s += 2;
        } else if (c0 == 0xC8 && (c1 == 0x99 || c1 == 0x9B)) { /* ș ț */
            out[n++] = (char)0xC8, out[n++] = (char)(c1 - 1), s += 2;
        } else {
            out[n++] = (char)(c0 >= 'a' && c0 <= 'z' ? c0 - 32 : c0);
            s++;
        }
    }
    out[n] = 0;
}

void ui_label_caps(Ui *ui, float x, float y, const char *s)
{
    char caps[128];
    to_caps(s, caps, sizeof(caps));
    const float size = k_ui.t_label, track = k_ui.label_tracking_em * size;
    const char *p = caps;
    while (*p) { /* glyph by glyph, with the tracking between */
        char one[5] = {0};
        int len = ((unsigned char)*p) < 0x80 ? 1 : ((unsigned char)*p & 0xE0) == 0xC0 ? 2 : ((unsigned char)*p & 0xF0) == 0xE0 ? 3 : 4;
        for (int i = 0; i < len && p[i]; i++) {
            one[i] = p[i];
        }
        x += ui_text(ui, TEXT_FACE_BOLD, x, y, size, C(k_ui.teal_text), one) + track;
        p += len;
    }
}

void ui_surface(Ui *ui, float x, float y, float w, float h, float radius, float lip, UiColor face, UiColor lip_c, float highlight, UiColor hl)
{
    const float r = radius < 0.0f ? -1.0f : radius;
    if (lip > 0.0f) {
        ui_shape(ui->gfx, &(UiShape){.x = x, .y = y + lip, .w = w, .h = h, .radius = r, .color = lip_c});
    }
    ui_shape(ui->gfx, &(UiShape){.x = x, .y = y, .w = w, .h = h, .radius = r, .highlight = highlight, .color = face, .color2 = hl});
}

void ui_card(Ui *ui, float x, float y, float w, float h)
{
    ui_block(ui, x, y, w, h + k_ui.lip_card);
    ui_surface(ui, x, y, w, h, k_ui.r_card, k_ui.lip_card, C(k_ui.surface), C(k_ui.lip_mint), 0.0f, C(0));
}

static void begin_squash(Ui *ui, uint32_t key, float cx, float bottom, float extra)
{
    float sx = 1.0f, sy = 1.0f;
    if (ui->anim) {
        sx = ui_anim_get(ui->anim, key, UI_SX, 1.0f) * extra;
        sy = ui_anim_get(ui->anim, key, UI_SY, 1.0f) * extra;
    }
    gfx_ui_group(ui->gfx, sx, 0.0f, 0.0f, sy, cx, bottom);
}

bool ui_button_v(Ui *ui, const char *label, float x, float y, float w, float h, UiVariant v, int flags)
{
    UiColor face, lip, tc;
    ui_variant_colors(v, &face, &lip, &tc);
    const uint32_t key = ui_key(label);
    bool held = false, clicked = false;
    if (v != UI_V_DISABLED) { /* disabled: no motion, no sound, not a target */
        clicked = ui_press(ui, label, x, y, w, h, &held, (flags & UI_F_QUIET) != 0);
    }
    const float lip_h = mg_minf(k_ui.lip_button, h * 0.13f), face_h = h - lip_h;
    float breathe = 1.0f;
    if ((flags & UI_F_BREATHE) && ui->anim && !ui->anim->reduced && !ui->modal && v != UI_V_DISABLED) {
        ui_anim_loop(ui->anim, key, UI_VALUE, 1.0f, k_ui.breathe_scale, k_ui.breathe_s);
        breathe = ui_anim_get(ui->anim, key, UI_VALUE, 1.0f);
    }
    const float dx = ui->anim ? ui_anim_get(ui->anim, key, UI_X, 0.0f) : 0.0f; /* shake */
    begin_squash(ui, key, x + dx + w * 0.5f, y + h, breathe);
    ui_surface(ui, x + dx, y, w, face_h, mg_minf(k_ui.r_button, face_h * 0.5f), lip_h, face, lip, k_ui.highlight_band,
               C(v == UI_V_ACCENT ? k_ui.highlight_accent : v == UI_V_DISABLED ? 0 : k_ui.highlight));
    char vis[96];
    const char *hash = strstr(label, "##");
    const size_t n = hash ? (size_t)(hash - label) : strlen(label);
    SDL_snprintf(vis, sizeof(vis), "%.*s", (int)(n < sizeof(vis) ? n : sizeof(vis) - 1), label);
    float size = (flags & UI_F_SMALL) ? k_ui.t_body : mg_minf(k_ui.t_button, face_h * 0.44f);
    const float tw = ui_text_w(ui, TEXT_FACE_DISPLAY, vis, size);
    if (tw > w - 20.0f && tw > 0.0f) {
        size *= (w - 20.0f) / tw;
    }
    ui_text_c(ui, TEXT_FACE_DISPLAY, x + dx + w * 0.5f, y + face_h * 0.5f + 1.0f, size, tc, vis);
    gfx_ui_group_off(ui->gfx);
    if (clicked && ui->fx) {
        ui_fx_tier(ui->fx, UI_T1, x + w * 0.5f, y + face_h * 0.5f, true);
    }
    return clicked;
}

bool ui_icon_button(Ui *ui, const char *id, UiIcon icon, float x, float y, float size, UiVariant v)
{
    UiColor face, lip, tc;
    ui_variant_colors(v, &face, &lip, &tc);
    const uint32_t key = ui_key(id);
    bool held = false, clicked = false;
    if (v != UI_V_DISABLED) {
        clicked = ui_press(ui, id, x, y, size, size, &held, false);
    }
    const float lip_h = mg_minf(k_ui.lip_button, size * 0.13f), face_h = size - lip_h;
    begin_squash(ui, key, x + size * 0.5f, y + size, 1.0f);
    ui_surface(ui, x, y, size, face_h, mg_minf(k_ui.r_button, face_h * 0.5f), lip_h, face, lip, k_ui.highlight_band,
               C(v == UI_V_ACCENT ? k_ui.highlight_accent : k_ui.highlight));
    ui_icon(ui, icon, x + size * 0.5f, y + face_h * 0.5f, mg_minf(k_ui.icon_box, face_h * 0.6f), tc);
    gfx_ui_group_off(ui->gfx);
    if (clicked && ui->fx) {
        ui_fx_tier(ui->fx, UI_T1, x + size * 0.5f, y + face_h * 0.5f, true);
    }
    return clicked;
}

bool ui_switch(Ui *ui, const char *id, float x, float y, bool *value)
{
    const uint32_t key = ui_key(id);
    bool held = false;
    const bool flipped = ui_press(ui, id, x, y, k_ui.toggle_w, k_ui.toggle_h, &held, false);
    float pos = *value ? 1.0f : 0.0f;
    if (flipped) {
        *value = !*value;
        if (ui->anim) { /* the knob: state spring (450 ms, overshoot), or 150 ms linear when reduced */
            const bool red = ui->anim->reduced;
            ui_anim_to(ui->anim, key, UI_VALUE, pos, *value ? 1.0f : 0.0f, red ? k_ui.reduced_s : k_ui.spring_s, red ? UI_EASE_LINEAR : UI_EASE_BEZIER, &k_ui.spring, false);
            ui_anim_to(ui->anim, key, UI_ALPHA, pos, *value ? 1.0f : 0.0f, k_ui.colour_s, UI_EASE_LINEAR, NULL, false);
        }
        if (ui->fx) {
            ui_fx_tier(ui->fx, UI_T1_TOGGLE, x + (*value ? k_ui.toggle_w - k_ui.toggle_h * 0.5f : k_ui.toggle_h * 0.5f), y + k_ui.toggle_h * 0.5f, *value);
        }
    }
    pos = ui->anim ? ui_anim_get(ui->anim, key, UI_VALUE, *value ? 1.0f : 0.0f) : (*value ? 1.0f : 0.0f);
    const float tint = ui->anim ? ui_anim_get(ui->anim, key, UI_ALPHA, *value ? 1.0f : 0.0f) : (*value ? 1.0f : 0.0f);
    const UiColor off = C(k_ui.off), on = C(k_ui.accent);
    const UiColor track = {(uint8_t)(off.r + (on.r - off.r) * tint), (uint8_t)(off.g + (on.g - off.g) * tint), (uint8_t)(off.b + (on.b - off.b) * tint), 255};
    ui_shape(ui->gfx, &(UiShape){.x = x, .y = y, .w = k_ui.toggle_w, .h = k_ui.toggle_h, .radius = -1.0f, .color = track});
    const float pad = (k_ui.toggle_h - k_ui.toggle_knob) * 0.5f, travel = k_ui.toggle_w - k_ui.toggle_h;
    const float kx = x + pad + travel * pos, ky = y + pad;
    ui_surface(ui, kx, ky - 1.0f, k_ui.toggle_knob, k_ui.toggle_knob - 2.0f, -1.0f, 2.0f, C(k_ui.surface), C(k_ui.lip_mint), 0.0f, C(0));
    return flipped;
}

bool ui_tabs(Ui *ui, const char *id, const char *const *labels, int n, int *sel, float x, float y, float w, float h)
{
    const uint32_t key = ui_key(id);
    const float lip = k_ui.lip_pill, inner = 4.0f, face_h = h - lip, seg = (w - 2.0f * inner) / (float)n;
    ui_surface(ui, x, y, w, face_h, k_ui.r_tab, lip, C(k_ui.surface), C(k_ui.lip_mint), 0.0f, C(0));
    bool changed = false;
    for (int i = 0; i < n; i++) {
        char tid[64];
        bool held = false;
        SDL_snprintf(tid, sizeof(tid), "%s%d", id, i);
        if (ui_press(ui, tid, x + inner + seg * (float)i, y, seg, face_h, &held, false) && i != *sel) {
            const float from = (float)*sel;
            *sel = i;
            changed = true;
            if (ui->anim) {
                const bool red = ui->anim->reduced;
                ui_anim_to(ui->anim, key, UI_VALUE, from, (float)i, red ? k_ui.reduced_s : k_ui.spring_s, red ? UI_EASE_LINEAR : UI_EASE_BEZIER, &k_ui.spring, false);
            }
            if (ui->fx) {
                ui_fx_tier(ui->fx, UI_T1_TAB, x + inner + seg * ((float)i + 0.5f), y + face_h * 0.5f, true);
            }
        }
    }
    const float at = ui->anim ? ui_anim_get(ui->anim, key, UI_VALUE, (float)*sel) : (float)*sel;
    ui_shape(ui->gfx, &(UiShape){.x = x + inner + seg * at, .y = y + inner, .w = seg, .h = face_h - 2.0f * inner, .radius = k_ui.r_tab - inner, .color = C(k_ui.ink)});
    for (int i = 0; i < n; i++) {
        const float near = 1.0f - mg_minf(fabsf(at - (float)i), 1.0f); /* the indicator over it: white text */
        const UiColor ink = C(k_ui.ink), white = C(0xFFFFFFFF);
        const UiColor tc = {(uint8_t)(ink.r + (white.r - ink.r) * near), (uint8_t)(ink.g + (white.g - ink.g) * near), (uint8_t)(ink.b + (white.b - ink.b) * near), 255};
        float size = k_ui.t_body;
        const float tw = ui_text_w(ui, TEXT_FACE_BOLD, labels[i], size);
        if (tw > seg - 12.0f && tw > 0.0f) {
            size *= (seg - 12.0f) / tw;
        }
        ui_text_c(ui, TEXT_FACE_BOLD, x + inner + seg * ((float)i + 0.5f), y + face_h * 0.5f, size, tc, labels[i]);
    }
    return changed;
}

void ui_counter(Ui *ui, const char *id, float x, float y, float h, const char *prefix, int value, const char *suffix, int *shown_last)
{
    const uint32_t key = ui_key(id);
    const int shown = ui->fx ? ui_fx_shown(ui->fx, key, value) : value;
    if (shown_last) {
        const bool in_flight = ui->fx && ui->fx->counter_key == key && ui->fx->landed_tokens < ui->fx->flight_tokens;
        if (*shown_last != shown && ui->anim && !in_flight && !ui->anim->reduced) { /* bump: 1 -> 1.22 -> 1, 200 ms */
            static const float bt[3] = {0.0f, 0.5f, 1.0f};
            const float bv[3] = {1.0f, k_ui.bump_scale, 1.0f};
            ui_anim_keys(ui->anim, key, UI_SX, 1.0f, bt, bv, 3, k_ui.bump_s, UI_EASE_OUT);
            ui_anim_keys(ui->anim, key, UI_SY, 1.0f, bt, bv, 3, k_ui.bump_s, UI_EASE_OUT);
        }
        *shown_last = shown;
    }
    char text[48];
    SDL_snprintf(text, sizeof(text), "%s%d%s", prefix ? prefix : "", shown, suffix ? suffix : "");
    const float lip = k_ui.lip_pill, face_h = h - lip, tw = ui_text_w(ui, TEXT_FACE_DISPLAY, text, face_h * 0.55f), w = tw + face_h;
    float sx = 1.0f, sy = 1.0f;
    if (ui->anim) {
        sx = ui_anim_get(ui->anim, key, UI_SX, 1.0f), sy = ui_anim_get(ui->anim, key, UI_SY, 1.0f);
    }
    gfx_ui_group(ui->gfx, sx, 0.0f, 0.0f, sy, x + w * 0.5f, y + face_h * 0.5f);
    ui_surface(ui, x, y, w, face_h, -1.0f, lip, C(k_ui.surface), C(k_ui.lip_mint), 0.0f, C(0));
    ui_text_c(ui, TEXT_FACE_DISPLAY, x + w * 0.5f, y + face_h * 0.5f + 1.0f, face_h * 0.55f, C(k_ui.ink), text);
    gfx_ui_group_off(ui->gfx);
    /* remember the rect for the reward flight's target */
    bool held;
    ui_press(ui, id, x, y, w, face_h, &held, true);
}

void ui_progress(Ui *ui, const char *id, float x, float y, float w, float h, float frac)
{
    const uint32_t key = ui_key(id);
    frac = frac < 0.0f ? 0.0f : frac > 1.0f ? 1.0f : frac;
    float shown = frac;
    if (ui->anim) { /* the fill holds its last target; a new value eases from where it is (700 ms, overshoot) */
        float to;
        if (!ui_anim_target(ui->anim, key, UI_VALUE, &to) || fabsf(to - frac) > 1e-4f) {
            const bool red = ui->anim->reduced;
            ui_anim_to(ui->anim, key, UI_VALUE, 0.0f, frac, red ? k_ui.reduced_s : k_ui.fill_s, red ? UI_EASE_LINEAR : UI_EASE_BEZIER, &k_ui.spring, true);
        }
        shown = ui_anim_get(ui->anim, key, UI_VALUE, frac);
    }
    ui_shape(ui->gfx, &(UiShape){.x = x, .y = y, .w = w, .h = h, .radius = -1.0f, .color = C(k_ui.lip_mint)});
    if (shown > 0.0f) {
        const float fw = mg_maxf(w * mg_minf(shown, 1.06f), h);
        ui_shape(ui->gfx, &(UiShape){.x = x, .y = y, .w = mg_minf(fw, w), .h = h, .radius = -1.0f, .highlight = mg_minf(k_ui.fill_highlight_band, h * 0.3f),
                                     .color = C(k_ui.fill), .color2 = C(k_ui.fill_highlight)});
    }
}

bool ui_row(Ui *ui, const char *id, float x, float y, float w, float h, bool tappable)
{
    bool held = false, clicked = false;
    const uint32_t key = ui_key(id);
    if (tappable) {
        clicked = ui_press(ui, id, x, y, w, h, &held, false);
    }
    const float lip = k_ui.lip_row;
    begin_squash(ui, key, x + w * 0.5f, y + h, 1.0f);
    ui_surface(ui, x, y, w, h - lip, k_ui.r_row, lip, C(k_ui.surface_tint), C(k_ui.secondary_lip), 0.0f, C(0));
    gfx_ui_group_off(ui->gfx);
    if (clicked && ui->fx) {
        ui_fx_sound(ui->fx, UI_SND_TICK, 0);
    }
    return clicked;
}

bool ui_answer(Ui *ui, const char *id, const char *text, float x, float y, float w, UiAnswer state)
{
    const float h = k_ui.row_h + k_ui.lip_row, lip = k_ui.lip_row;
    const uint32_t key = ui_key(id);
    bool held = false;
    const bool clicked = state == UI_ANS_DEFAULT && ui_press(ui, id, x, y, w, h, &held, false);
    UiColor face = C(k_ui.surface_tint), lip_c = C(k_ui.secondary_lip), tc = C(k_ui.ink);
    if (state == UI_ANS_OK) {
        face = C(k_ui.ok), lip_c = C(k_ui.ok_lip), tc = C(0xFFFFFFFF);
    } else if (state == UI_ANS_BAD) {
        face = C(k_ui.bad), lip_c = C(k_ui.bad_lip), tc = C(0xFFFFFFFF);
    }
    const float alpha = state == UI_ANS_DIM ? 0.55f : 1.0f;
    const float dx = ui->anim ? ui_anim_get(ui->anim, key, UI_X, 0.0f) : 0.0f;
    begin_squash(ui, key, x + w * 0.5f, y + h, 1.0f);
    ui_surface(ui, x + dx, y, w, h - lip, k_ui.r_row, lip, with_alpha(face, alpha), with_alpha(lip_c, alpha), 0.0f, C(0));
    if (state == UI_ANS_REVEAL) { /* the right one, shown after a wrong pick: a 3 pt ring */
        ui_shape(ui->gfx, &(UiShape){.x = x + dx, .y = y, .w = w, .h = h - lip, .radius = k_ui.r_row, .stroke = k_ui.ring_width, .color = C(k_ui.ok)});
    }
    ui_text(ui, TEXT_FACE_BOLD, x + dx + 16.0f, y + (h - lip) * 0.5f - k_ui.t_body * 0.62f, k_ui.t_body, with_alpha(tc, alpha), text);
    if (state == UI_ANS_OK || state == UI_ANS_REVEAL) {
        ui_icon(ui, UI_ICON_CHECK, x + dx + w - 26.0f, y + (h - lip) * 0.5f, k_ui.icon_box, state == UI_ANS_OK ? tc : C(k_ui.ok));
    } else if (state == UI_ANS_BAD) {
        ui_icon(ui, UI_ICON_CLOSE, x + dx + w - 26.0f, y + (h - lip) * 0.5f, k_ui.icon_box, tc);
    }
    gfx_ui_group_off(ui->gfx);
    return clicked;
}

void ui_modal(Ui *ui, const char *id, float w, float h, float *x, float *y)
{
    const uint32_t key = ui_key(id);
    float W = 0.0f, H = 0.0f;
    gfx_ui_size(ui->gfx, &W, &H);
    ui->modal = true;
    ui_block(ui, 0.0f, 0.0f, W, H);
    ui_rect(ui->gfx, ui->font, 0.0f, 0.0f, W, H, C(k_ui.scrim)); /* flat scrim, no blur */
    *x = (W - w) * 0.5f, *y = (H - h) * 0.5f;
    if (ui->anim && ui_anim_fresh(ui->anim, key)) { /* enter: from 0.8 (550 ms, overshoot), or a 150 ms move when reduced */
        const bool red = ui->anim->reduced;
        ui_anim_to(ui->anim, key, UI_SX, red ? 0.96f : 0.8f, 1.0f, red ? k_ui.reduced_s : k_ui.enter_s, red ? UI_EASE_LINEAR : UI_EASE_BEZIER, &k_ui.enter, false);
    }
    const float s = ui->anim ? ui_anim_get(ui->anim, key, UI_SX, 1.0f) : 1.0f;
    ui_card(ui, *x, *y, w, h);
    gfx_ui_group(ui->gfx, s, 0.0f, 0.0f, s, *x + w * 0.5f, *y + h * 0.5f); /* the card and what the caller draws in it */
}

void ui_modal_end(Ui *ui) { gfx_ui_group_off(ui->gfx); }

void ui_shake(Ui *ui, const char *id, float amp)
{
    if (!ui->anim || ui->anim->reduced) {
        return;
    }
    float t[8], v[8];
    for (int i = 0; i < 8; i++) {
        t[i] = (float)i / 7.0f;
        v[i] = k_ui.shake_x[i] * amp;
    }
    ui_anim_keys(ui->anim, ui_key(id), UI_X, 0.0f, t, v, 8, k_ui.shake_s, UI_EASE_LINEAR);
}

float ui_shake_x(const Ui *ui, const char *id) { return ui->anim && id ? ui_anim_get(ui->anim, ui_key(id), UI_X, 0.0f) : 0.0f; }

void ui_pop(Ui *ui, const char *id)
{
    if (!ui->anim || ui->anim->reduced) {
        return;
    }
    static const float t[3] = {0.0f, 0.4f, 1.0f};
    const float v[3] = {1.0f, k_ui.pop_scale, 1.0f};
    ui_anim_keys(ui->anim, ui_key(id), UI_SX, 1.0f, t, v, 3, k_ui.pop_s, UI_EASE_OUT);
    ui_anim_keys(ui->anim, ui_key(id), UI_SY, 1.0f, t, v, 3, k_ui.pop_s, UI_EASE_OUT);
}

void ui_tier_at(Ui *ui, const char *id, UiTier tier)
{
    float x, y, w, h;
    if (ui->fx && ui_rect_of(ui, id, &x, &y, &w, &h)) {
        ui_fx_tier(ui->fx, tier, x + w * 0.5f, y + h * 0.5f, true);
    }
}
