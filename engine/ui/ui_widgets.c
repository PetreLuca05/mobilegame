#include "ui_widgets.h"

#include "haptics.h"
#include "text_check.h"

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

const TextFont *ui_face(const Ui *ui, int face) { return face_of(ui, face); }

float ui_text(Ui *ui, int face, float x, float y, float size, UiColor c, const char *s) { return text_draw(ui->gfx, face_of(ui, face), x, y, size, c, s); }

void ui_text_c(Ui *ui, int face, float cx, float cy, float size, UiColor c, const char *s) { text_draw_centered(ui->gfx, face_of(ui, face), cx, cy, size, c, s); }

float ui_text_w(const Ui *ui, int face, const char *s, float size) { return text_width(face_of(ui, face), s, size); }

void ui_variant_colors(UiVariant v, UiColor *face, UiColor *lip, UiColor *text)
{
    *face = C(v == UI_V_PRIMARY ? k_ui.primary : v == UI_V_SECONDARY ? k_ui.surface : v == UI_V_ACCENT ? k_ui.accent : v == UI_V_DESTRUCTIVE ? k_ui.bad : v == UI_V_OK ? k_ui.ok : k_ui.disabled);
    *lip = C(v == UI_V_PRIMARY ? k_ui.primary_lip : v == UI_V_SECONDARY ? k_ui.secondary_lip : v == UI_V_ACCENT ? k_ui.accent_lip : v == UI_V_DESTRUCTIVE ? k_ui.bad_lip : v == UI_V_OK ? k_ui.ok_lip : k_ui.disabled_lip);
    *text = C(v == UI_V_PRIMARY || v == UI_V_SECONDARY ? k_ui.ink : v == UI_V_DISABLED ? k_ui.disabled_text : k_ui.white);
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
    case UI_ICON_DICE: { /* a die showing five: a rounded square outline and five pips */
        ui_shape(ui->gfx, &(UiShape){.x = x0 + 0.16f * size, .y = y0 + 0.16f * size, .w = 0.68f * size, .h = 0.68f * size, .radius = 5.0f * s, .stroke = w, .color = c});
        static const float pip[5][2] = {{0.35f, 0.35f}, {0.65f, 0.35f}, {0.5f, 0.5f}, {0.35f, 0.65f}, {0.65f, 0.65f}};
        for (int i = 0; i < 5; i++) {
            ui_circle(ui->gfx, ui->font, x0 + pip[i][0] * size, y0 + pip[i][1] * size, 0.065f * size, c);
        }
        break;
    }
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
    text_check_scan(caps, (size_t)-1); /* checked whole: the glyphs below are drawn one by one */
    text_check_exempt_push();
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
    text_check_exempt_pop();
}

void ui_depth(Ui *ui, int level, uint32_t key, int flags)
{
    const float d = mg_maxf(k_ui.tilt_depth[level < 0 ? 0 : level > 4 ? 4 : level], ui->depth_base); /* near an edge the shader clamps it */
    if (key && ui->active_id == key && ui->active_touch >= 0) { /* pressed: the shift holds still under the finger */
        gfx_ui_depth(ui->gfx, 0.0f, flags);
        gfx_ui_offset(ui->gfx, -ui->freeze_x * d, -ui->freeze_y * d);
    } else {
        gfx_ui_depth(ui->gfx, d, flags);
        gfx_ui_offset(ui->gfx, 0.0f, 0.0f);
    }
}

void ui_jiggle(Ui *ui, float dir, double now)
{
    ui->jiggle_count = 0;
    for (int i = 0; i < ui->seen_count && ui->jiggle_count < 64; i++) {
        if (ui->seen[i].rect_only) {
            continue;
        }
        float w, h;
        gfx_ui_size(ui->gfx, &w, &h);
        const float cx = ui->seen[i].x + ui->seen[i].w * 0.5f;
        if (cx < 0.0f || cx > w || ui->seen[i].y + ui->seen[i].h < 0.0f || ui->seen[i].y > h) {
            continue;
        }
        const float from = dir > 0.0f ? (w - cx) / w : cx / w; /* 0 at the side that moved first */
        ui->jiggle[ui->jiggle_count].key = ui->seen[i].key;
        ui->jiggle[ui->jiggle_count].at = now + (double)(from * w / 60.0f) * k_ui.tilt_jiggle_stagger_s; /* 15 ms per 60 pt across */
        ui->jiggle_count++;
    }
    ui_haptic(HAPTIC_TILT_JIGGLE);
}

void ui_jiggle_update(Ui *ui)
{
    if (!ui->anim) {
        return;
    }
    for (int i = 0; i < ui->jiggle_count;) {
        if (ui->anim->now >= ui->jiggle[i].at) {
            ui_anim_keys(ui->anim, ui->jiggle[i].key, UI_SX, 1.0f, k_ui.release_t, k_ui.release_sx, 5, k_ui.release_s, UI_EASE_OUT);
            ui_anim_keys(ui->anim, ui->jiggle[i].key, UI_SY, 1.0f, k_ui.release_t, k_ui.release_sy, 5, k_ui.release_s, UI_EASE_OUT);
            ui->jiggles_started++;
            ui->jiggle[i] = ui->jiggle[--ui->jiggle_count];
        } else {
            i++;
        }
    }
}

void ui_depth_end(Ui *ui)
{
    gfx_ui_depth(ui->gfx, ui->depth_base, 0);
    gfx_ui_offset(ui->gfx, 0.0f, 0.0f);
}

void ui_surface(Ui *ui, float x, float y, float w, float h, float radius, float lip, UiColor face, UiColor lip_c, float highlight, UiColor hl)
{
    const float r = radius < 0.0f ? -1.0f : radius;
    if (lip > 0.0f) { /* the lip swings with the tilt (TILT-1) */
        ui_shape(ui->gfx, &(UiShape){.x = x, .y = y + lip, .w = w, .h = h, .radius = r, .lip = lip, .color = lip_c});
    }
    ui_shape(ui->gfx, &(UiShape){.x = x, .y = y, .w = w, .h = h, .radius = r, .highlight = highlight, .color = face, .color2 = hl});
}

void ui_card(Ui *ui, float x, float y, float w, float h)
{
    ui_block(ui, x, y, w, h + k_ui.lip_card);
    ui_depth(ui, 1, 0, 0);
    ui_surface(ui, x, y, w, h, k_ui.r_card, k_ui.lip_card, C(k_ui.surface), C(k_ui.lip_mint), 0.0f, C(0));
    ui_depth_end(ui);
}

float ui_tag_chip_w(const Ui *ui, const char *text) { return ui_text_w(ui, TEXT_FACE_BOLD, text, k_ui.t_label) + 2.0f * k_ui.tag_pad; }

float ui_tag_chip(Ui *ui, const char *text, float right, float top)
{
    const float w = ui_tag_chip_w(ui, text), h = k_ui.tag_h, x = right - w;
    ui_surface(ui, x, top, w, h, -1.0f, k_ui.tag_lip, C(k_ui.primary), C(k_ui.primary_lip), k_ui.highlight_band, C(k_ui.highlight));
    ui_text_c(ui, TEXT_FACE_BOLD, x + w * 0.5f, top + h * 0.5f, k_ui.t_label, C(k_ui.ink), text);
    return w;
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

/* A button's face on its lip. A secondary face is white like the cards it sits on, so it gets an outline in
 * its lip colour (one shape: filled, with a border) instead of the white highlight band nobody would see. */
static void button_face(Ui *ui, float x, float y, float w, float face_h, float lip_h, UiVariant v, UiColor face, UiColor lip)
{
    const float r = mg_minf(k_ui.r_button, face_h * 0.5f);
    if (v != UI_V_SECONDARY) {
        ui_surface(ui, x, y, w, face_h, r, lip_h, face, lip, k_ui.highlight_band,
                   C(v == UI_V_ACCENT ? k_ui.highlight_accent : v == UI_V_DISABLED ? 0 : k_ui.highlight));
        return;
    }
    ui_shape(ui->gfx, &(UiShape){.x = x, .y = y + lip_h, .w = w, .h = face_h, .radius = r, .lip = lip_h, .color = lip});
    ui_shape(ui->gfx, &(UiShape){.x = x, .y = y, .w = w, .h = face_h, .radius = r, .stroke = -k_ui.edge_secondary, .color = face, .color2 = C(k_ui.secondary_lip)});
}

/* the haptic row of a button: its variant, the screen's main action (the breathing one), a destructive modal's confirm */
static int button_class(UiVariant v, int flags)
{
    if (flags & UI_F_STEPPER) {
        return HC_STEPPER;
    }
    if (flags & UI_F_RANDOMISE) {
        return HC_RANDOMISE;
    }
    if (flags & UI_F_LINK) {
        return HC_LINK;
    }
    switch (v) {
    case UI_V_PRIMARY:
    case UI_V_OK:
        return (flags & (UI_F_MAIN | UI_F_BREATHE)) ? HC_MAIN : HC_PRIMARY;
    case UI_V_DESTRUCTIVE:
        return (flags & UI_F_CONFIRM) ? HC_CONFIRM : HC_DESTRUCTIVE;
    case UI_V_DISABLED:
        return HC_NONE;
    default:
        return (flags & UI_F_MAIN) ? HC_MAIN : HC_SECONDARY;
    }
}

bool ui_button_v(Ui *ui, const char *label, float x, float y, float w, float h, UiVariant v, int flags)
{
    UiColor face, lip, tc;
    ui_variant_colors(v, &face, &lip, &tc);
    const uint32_t key = ui_key(label);
    bool held = false, clicked = false;
    if (v != UI_V_DISABLED) { /* disabled: no motion, no sound, no haptic, not a target */
        ui->hclass = button_class(v, flags);
        clicked = ui_press(ui, label, x, y, w, h, &held, (flags & UI_F_QUIET) != 0);
    }
    const float lip_h = mg_minf(k_ui.lip_button, h * 0.13f), face_h = h - lip_h;
    ui_depth(ui, 2, key, v == UI_V_PRIMARY || v == UI_V_OK ? GFX_TILT_GLOSS : 0); /* a control on a card; primary faces gloss */
    float breathe = 1.0f;
    if ((flags & UI_F_BREATHE) && ui->anim && !ui->anim->reduced && !ui->modal && v != UI_V_DISABLED) {
        ui_anim_loop(ui->anim, key, UI_VALUE, 1.0f, k_ui.breathe_scale, k_ui.breathe_s);
        breathe = ui_anim_get(ui->anim, key, UI_VALUE, 1.0f);
    }
    const float dx = ui->anim ? ui_anim_get(ui->anim, key, UI_X, 0.0f) : 0.0f; /* shake */
    begin_squash(ui, key, x + dx + w * 0.5f, y + h, breathe);
    button_face(ui, x + dx, y, w, face_h, lip_h, v, face, lip);
    char vis[96];
    const char *hash = strstr(label, "##");
    const size_t n = hash ? (size_t)(hash - label) : strlen(label);
    SDL_snprintf(vis, sizeof(vis), "%.*s", (int)(n < sizeof(vis) ? n : sizeof(vis) - 1), label);
    float size = (flags & UI_F_SMALL) ? k_ui.t_body : mg_minf(k_ui.t_button, face_h * 0.44f);
    const float tw = ui_text_w(ui, TEXT_FACE_DISPLAY, vis, size);
    if (tw > w - 20.0f && tw > 0.0f) {
        size *= (w - 20.0f) / tw;
    }
    ui_text_c(ui, TEXT_FACE_DISPLAY, x + dx + w * 0.5f, y + face_h * 0.5f, size, tc, vis); /* centred on the face, not the lip */
    gfx_ui_group_off(ui->gfx);
    ui_depth_end(ui);
    if (clicked && ui->fx) {
        ui_fx_tier(ui->fx, UI_T1, x + w * 0.5f, y + face_h * 0.5f, true);
    }
    return clicked;
}

UiSideLayout ui_side_layout(float safe_l, float safe_t, float safe_r, float safe_b, float w_pt, float h_pt, float header_w)
{
    const float pad = 16.0f;
    UiSideLayout L;
    L.x0 = safe_l + pad;
    L.y0 = mg_maxf(safe_t, 8.0f) + 8.0f;
    L.bottom = h_pt - mg_maxf(safe_b, 12.0f);
    const float inner_w = w_pt - safe_l - safe_r - 2.0f * pad;
    L.col_w = mg_clampf(header_w, 160.0f, w_pt * 0.5f - pad - L.x0);
    L.content_x = L.x0 + L.col_w + 2.0f * pad;
    L.content_w = L.x0 + inner_w - L.content_x;
    return L;
}

float ui_side_tabs(Ui *ui, const UiSideLayout *L, const char *id, const char *const *labels, const int *values, const int *badges, int n,
                   int *sel, float y)
{
    for (int t = 0; t < n; t++) {
        const int v = values ? values[t] : t;
        char label[96];
        SDL_snprintf(label, sizeof(label), "%s##%s%d", labels[t], id, v);
        if (ui_button_v(ui, label, L->x0, y, L->col_w, 50.0f, *sel == v ? UI_V_PRIMARY : UI_V_SECONDARY, 0)) {
            *sel = v;
        }
        if (badges && badges[t] > 0) {
            char num[8];
            SDL_snprintf(num, sizeof(num), "%d", badges[t]);
            ui_circle(ui->gfx, ui->font, L->x0 + L->col_w - 22.0f, y + 22.0f, 12.0f, C(k_ui.bad));
            text_draw_centered(ui->gfx, ui->font, L->x0 + L->col_w - 22.0f, y + 22.0f, 13.0f, C(k_ui.white), num);
        }
        y += 58.0f;
    }
    ui_rect(ui->gfx, ui->font, L->x0 + L->col_w + 16.0f, L->y0, 2.0f, L->bottom - L->y0, C(k_ui.lip_mint)); /* column | content */
    return y;
}

static float s_press_cx, s_press_cy; /* the open pressable's centre (its T1) */
static bool s_press_clicked;

bool ui_pressable_begin(Ui *ui, const char *id, float x, float y, float w, float h, int hclass)
{
    bool held = false;
    ui->hclass = hclass;
    s_press_clicked = ui_press(ui, id, x, y, w, h, &held, false);
    s_press_cx = x + w * 0.5f, s_press_cy = y + h * 0.5f;
    ui_depth(ui, 2, ui_key(id), 0);
    begin_squash(ui, ui_key(id), x + w * 0.5f, y + h, 1.0f);
    return s_press_clicked;
}

void ui_pressable_end(Ui *ui)
{
    gfx_ui_group_off(ui->gfx);
    ui_depth_end(ui);
    if (s_press_clicked && ui->fx) {
        ui_fx_tier(ui->fx, UI_T1, s_press_cx, s_press_cy, true);
    }
    s_press_clicked = false;
}

/* The sign-in buttons by their owners' rules, not restyled (no lip, no squash): the official marks from the head atlas' cells
 * 62 (Google "G") and 63 (Apple logo-only artwork, only when the file is in: ui->brand_apple_logo). Google (light theme): white,
 * 1 pt inside stroke #747775, text #1F1F1F, the G 20 pt, 16 pt before it, 12 after, 16 after the text. Apple (black style on a
 * light card): black, white title and logo, the title 43 % of the button's height, the logo file as tall as the button, at least
 * 8 % of the width after the title. The group is centred; a press darkens the face a little. */
bool ui_button_brand(Ui *ui, const char *label, float x, float y, float w, float h, UiBrand brand, bool enabled)
{
    const bool g = brand == UI_BRAND_GOOGLE;
    const float a = enabled ? 1.0f : 0.45f; /* disabled: the same colours, faded (not a target) */
    UiColor face = C(g ? k_ui.google_face : k_ui.apple_face), edge = C(g ? k_ui.google_lip : k_ui.apple_face), tc = C(g ? k_ui.google_text : k_ui.apple_text);
    face.a = (uint8_t)(face.a * a), edge.a = (uint8_t)(edge.a * a), tc.a = (uint8_t)(tc.a * a);
    bool held = false, clicked = false;
    if (enabled) {
        ui->hclass = HC_SECONDARY;
        clicked = ui_press(ui, label, x, y, w, h, &held, false);
    }
    ui_depth(ui, 2, ui_key(label), 0);
    ui_shape(ui->gfx, &(UiShape){.x = x, .y = y, .w = w, .h = h, .radius = h * 0.5f, .stroke = g ? -1.0f : 0.0f, .color = face, .color2 = edge});
    if (held) {
        ui_shape(ui->gfx, &(UiShape){.x = x, .y = y, .w = w, .h = h, .radius = h * 0.5f, .color = C(g ? k_ui.google_press : k_ui.apple_press)});
    }
    char vis[96];
    const char *hash = strstr(label, "##");
    const size_t n = hash ? (size_t)(hash - label) : strlen(label);
    SDL_snprintf(vis, sizeof(vis), "%.*s", (int)(n < sizeof(vis) ? n : sizeof(vis) - 1), label);
    const float cy = y + h * 0.5f;
    const UiColor white = {255, 255, 255, (uint8_t)(255.0f * a)};
    if (g) {
        const float logo = 20.0f, size = 15.0f, tw = ui_text_w(ui, TEXT_FACE_BOLD, vis, size);
        const float group = 16.0f + logo + 12.0f + tw + 16.0f, gx = x + mg_maxf(0.0f, (w - group) * 0.5f);
        ui_atlas_image(ui->gfx, 62, gx + 16.0f, cy - logo * 0.5f, logo, logo, white);
        ui_text(ui, TEXT_FACE_BOLD, gx + 16.0f + logo + 12.0f, cy - size * 0.62f, size, tc, vis);
    } else {
        const float size = h * 0.43f, logo = ui->brand_apple_logo ? h : 0.0f, tw = ui_text_w(ui, TEXT_FACE_BOLD, vis, size);
        const float group = logo + tw, gx = x + mg_maxf(0.0f, mg_minf((w - group) * 0.5f, w * 0.92f - group)); /* >= 8 % after the title */
        if (logo > 0.0f) {
            ui_atlas_image(ui->gfx, 63, gx, y, logo, logo, white); /* the file carries its own padding: as tall as the button */
        }
        ui_text(ui, TEXT_FACE_BOLD, gx + logo, cy - size * 0.62f, size, tc, vis);
    }
    ui_depth_end(ui);
    if (clicked && ui->fx) {
        ui_fx_tier(ui->fx, UI_T1, x + w * 0.5f, cy, true);
    }
    return clicked;
}

bool ui_icon_button(Ui *ui, const char *id, UiIcon icon, float x, float y, float size, UiVariant v)
{
    return ui_icon_button_f(ui, id, icon, x, y, size, v, 0);
}

bool ui_icon_button_f(Ui *ui, const char *id, UiIcon icon, float x, float y, float size, UiVariant v, int flags)
{
    UiColor face, lip, tc;
    ui_variant_colors(v, &face, &lip, &tc);
    const uint32_t key = ui_key(id);
    bool held = false, clicked = false;
    if (v != UI_V_DISABLED) {
        ui->hclass = flags ? button_class(v, flags) : HC_SECONDARY; /* icon buttons: the secondary row, unless flagged */
        clicked = ui_press(ui, id, x, y, size, size, &held, false);
    }
    const float lip_h = mg_minf(k_ui.lip_button, size * 0.13f), face_h = size - lip_h;
    ui_depth(ui, 2, key, 0);
    begin_squash(ui, key, x + size * 0.5f, y + size, 1.0f);
    button_face(ui, x, y, size, face_h, lip_h, v, face, lip);
    ui_icon(ui, icon, x + size * 0.5f, y + face_h * 0.5f, mg_minf(k_ui.icon_box, face_h * 0.6f), tc);
    gfx_ui_group_off(ui->gfx);
    ui_depth_end(ui);
    if (clicked && ui->fx) {
        ui_fx_tier(ui->fx, UI_T1, x + size * 0.5f, y + face_h * 0.5f, true);
    }
    return clicked;
}

bool ui_switch(Ui *ui, const char *id, float x, float y, bool *value)
{
    const uint32_t key = ui_key(id);
    bool held = false;
    ui->hclass = HC_NONE; /* the toggle's haptic depends on the new state: played below */
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
            ui_haptic(*value ? HAPTIC_TOGGLE_ON : HAPTIC_TOGGLE_OFF);
            ui_fx_tier(ui->fx, UI_T1_TOGGLE, x + (*value ? k_ui.toggle_w - k_ui.toggle_h * 0.5f : k_ui.toggle_h * 0.5f), y + k_ui.toggle_h * 0.5f, *value);
        }
    }
    pos = ui->anim ? ui_anim_get(ui->anim, key, UI_VALUE, *value ? 1.0f : 0.0f) : (*value ? 1.0f : 0.0f);
    const float tint = ui->anim ? ui_anim_get(ui->anim, key, UI_ALPHA, *value ? 1.0f : 0.0f) : (*value ? 1.0f : 0.0f);
    const UiColor off = C(k_ui.off), on = C(k_ui.accent);
    const UiColor track = {(uint8_t)(off.r + (on.r - off.r) * tint), (uint8_t)(off.g + (on.g - off.g) * tint), (uint8_t)(off.b + (on.b - off.b) * tint), 255};
    ui_depth(ui, 2, key, 0);
    ui_shape(ui->gfx, &(UiShape){.x = x, .y = y, .w = k_ui.toggle_w, .h = k_ui.toggle_h, .radius = -1.0f, .color = track});
    const float pad = (k_ui.toggle_h - k_ui.toggle_knob) * 0.5f, travel = k_ui.toggle_w - k_ui.toggle_h;
    const float kx = x + pad + travel * pos, ky = y + pad;
    ui_surface(ui, kx, ky - 1.0f, k_ui.toggle_knob, k_ui.toggle_knob - 2.0f, -1.0f, 2.0f, C(k_ui.surface), C(k_ui.lip_mint), 0.0f, C(0));
    ui_depth_end(ui);
    return flipped;
}

bool ui_tabs(Ui *ui, const char *id, const char *const *labels, int n, int *sel, float x, float y, float w, float h)
{
    const uint32_t key = ui_key(id);
    const float lip = k_ui.lip_pill, inner = 4.0f, face_h = h - lip, seg = (w - 2.0f * inner) / (float)n;
    ui_depth(ui, 2, 0, 0);
    ui_surface(ui, x, y, w, face_h, k_ui.r_tab, lip, C(k_ui.surface), C(k_ui.lip_mint), 0.0f, C(0));
    bool changed = false;
    for (int i = 0; i < n; i++) {
        char tid[64];
        bool held = false;
        SDL_snprintf(tid, sizeof(tid), "%s%d", id, i);
        ui->hclass = HC_TAB;
        if (ui_press(ui, tid, x + inner + seg * (float)i, y, seg, h, &held, false) && i != *sel) { /* the lip is part of the target */
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
        const UiColor ink = C(k_ui.ink), white = C(k_ui.white);
        const UiColor tc = {(uint8_t)(ink.r + (white.r - ink.r) * near), (uint8_t)(ink.g + (white.g - ink.g) * near), (uint8_t)(ink.b + (white.b - ink.b) * near), 255};
        float size = k_ui.t_body;
        const float tw = ui_text_w(ui, TEXT_FACE_BOLD, labels[i], size);
        if (tw > seg - 12.0f && tw > 0.0f) {
            size *= (seg - 12.0f) / tw;
        }
        ui_text_c(ui, TEXT_FACE_BOLD, x + inner + seg * ((float)i + 0.5f), y + face_h * 0.5f, size, tc, labels[i]);
    }
    ui_depth_end(ui);
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
    ui_depth(ui, 3, 0, 0); /* a badge */
    gfx_ui_group(ui->gfx, sx, 0.0f, 0.0f, sy, x + w * 0.5f, y + face_h * 0.5f);
    ui_surface(ui, x, y, w, face_h, -1.0f, lip, C(k_ui.surface), C(k_ui.lip_mint), 0.0f, C(0));
    ui_text_c(ui, TEXT_FACE_DISPLAY, x + w * 0.5f, y + face_h * 0.5f, face_h * 0.55f, C(k_ui.ink), text);
    gfx_ui_group_off(ui->gfx);
    ui_depth_end(ui);
    ui_remember(ui, id, x, y, w, face_h); /* the reward flight's target, not a button */
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
    ui->hclass = HC_ANSWER;
    const bool clicked = state == UI_ANS_DEFAULT && ui_press(ui, id, x, y, w, h, &held, false);
    ui->hclass = HC_LINK;
    UiColor face = C(k_ui.surface_tint), lip_c = C(k_ui.secondary_lip), tc = C(k_ui.ink);
    if (state == UI_ANS_OK) {
        face = C(k_ui.ok), lip_c = C(k_ui.ok_lip), tc = C(k_ui.white);
    } else if (state == UI_ANS_BAD) {
        face = C(k_ui.bad), lip_c = C(k_ui.bad_lip), tc = C(k_ui.white);
    }
    const float alpha = state == UI_ANS_DIM ? 0.55f : 1.0f;
    const float dx = ui->anim ? ui_anim_get(ui->anim, key, UI_X, 0.0f) : 0.0f;
    ui_depth(ui, 2, key, 0);
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
    ui_depth_end(ui);
    return clicked;
}

static void modal_at(Ui *ui, const char *id, float w, float h, float *x, float *y, bool warning, bool placed);
static void modal(Ui *ui, const char *id, float w, float h, float *x, float *y, bool warning) { modal_at(ui, id, w, h, x, y, warning, false); }

void ui_modal_rect(Ui *ui, const char *id, float x, float y, float w, float h)
{
    float px = x, py = y;
    modal_at(ui, id, w, h, &px, &py, false, true);
}

/* placed: the caller chose (*x, *y) (a card inside the safe rect); else centred on the screen */
static void modal_at(Ui *ui, const char *id, float w, float h, float *x, float *y, bool warning, bool placed)
{
    const uint32_t key = ui_key(id);
    if (ui->modal_prev != key) { /* it opens this frame */
        ui_haptic(warning ? HAPTIC_WARNING_MODAL : HAPTIC_SURFACE_OPEN);
    }
    ui->modal_key = key;
    float W = 0.0f, H = 0.0f;
    gfx_ui_size(ui->gfx, &W, &H);
    ui->modal = true;
    ui_block(ui, 0.0f, 0.0f, W, H);
    gfx_ui_depth(ui->gfx, 0.0f, 0);
    ui_rect(ui->gfx, ui->font, 0.0f, 0.0f, W, H, C(k_ui.scrim)); /* flat scrim, no blur; it does not move */
    ui->depth_base = k_ui.tilt_depth[4];                         /* a modal and everything in it: the nearest layer */
    ui_depth_end(ui);
    if (!placed) {
        *x = (W - w) * 0.5f, *y = (H - h) * 0.5f;
    }
    if (ui->anim && ui_anim_fresh(ui->anim, key)) { /* enter: from 0.8 (550 ms, overshoot), or a 150 ms move when reduced */
        const bool red = ui->anim->reduced;
        ui_anim_to(ui->anim, key, UI_SX, red ? 0.96f : 0.8f, 1.0f, red ? k_ui.reduced_s : k_ui.enter_s, red ? UI_EASE_LINEAR : UI_EASE_BEZIER, &k_ui.enter, false);
    }
    const float s = ui->anim ? ui_anim_get(ui->anim, key, UI_SX, 1.0f) : 1.0f;
    ui_card(ui, *x, *y, w, h);
    gfx_ui_group(ui->gfx, s, 0.0f, 0.0f, s, *x + w * 0.5f, *y + h * 0.5f); /* the card and what the caller draws in it */
}

void ui_modal(Ui *ui, const char *id, float w, float h, float *x, float *y) { modal(ui, id, w, h, x, y, false); }
void ui_modal_warning(Ui *ui, const char *id, float w, float h, float *x, float *y) { modal(ui, id, w, h, x, y, true); }

void ui_modal_end(Ui *ui)
{
    gfx_ui_group_off(ui->gfx);
    ui->depth_base = k_ui.tilt_depth[1];
    ui_depth_end(ui);
}

#define UI_ENTER_DX 56.0f /* pt: where a screen's layer comes in from (the left edge side) */

/* dx and alpha of something that entered `t` seconds ago */
static void enter_at(const Ui *ui, float t, float *dx, float *alpha)
{
    const bool reduced = ui->anim && ui->anim->reduced;
    if (reduced) {
        *dx = 0.0f, *alpha = mg_saturate(t / k_ui.reduced_s);
        return;
    }
    const float u = mg_saturate(t / k_ui.enter_s);
    *dx = -UI_ENTER_DX * (1.0f - ui_bezier(&k_ui.enter, u));
    *alpha = mg_saturate(t / k_ui.exit_fade_s);
}

void ui_screen_enter(Ui *ui, float age, float shake_x)
{
    float dx, alpha;
    enter_at(ui, age, &dx, &alpha);
    ui->scr_age = age;
    ui->scr_dx = dx + shake_x, ui->scr_alpha = alpha;
    gfx_ui_screen(ui->gfx, ui->scr_dx, 0.0f, ui->scr_alpha);
}

void ui_stagger(Ui *ui, int index)
{
    const int k = index < 0 ? 0 : index < k_ui.stagger_max - 1 ? index : k_ui.stagger_max - 1;
    float dx, alpha;
    enter_at(ui, ui->scr_age - k_ui.stagger_s * (float)k, &dx, &alpha);
    gfx_ui_screen(ui->gfx, ui->scr_dx + dx, 0.0f, ui->scr_alpha * alpha);
}

void ui_stagger_end(Ui *ui) { gfx_ui_screen(ui->gfx, ui->scr_dx, 0.0f, ui->scr_alpha); }

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
