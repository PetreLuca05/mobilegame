#include "widgets.h"

#include <math.h>
#include <string.h>

#include "ui_widgets.h"

/* UI-1: the old theme names on the token table (surfaces are white now: text on them is ink) */
const UiTheme k_ui_theme = {
    .panel = {255, 255, 255, 255},
    .button = {255, 255, 255, 255},
    .button_down = {234, 247, 243, 255},
    .accent = {14, 143, 131, 255},
    .text = {23, 58, 58, 255},
    .text_dim = {42, 87, 83, 255},
};

static uint32_t hash_label(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return h ? h : 1u;
}

/* visible part of "Text##id" */
static void visible(const char *label, char *out, size_t cap)
{
    const char *hash = strstr(label, "##");
    size_t n = hash ? (size_t)(hash - label) : strlen(label);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, label, n);
    out[n] = 0;
}

static bool inside(const MgTouch *t, float x, float y, float w, float h, bool start)
{
    float px = start ? t->x0 : t->x;
    float py = start ? t->y0 : t->y;
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

uint32_t ui_key(const char *label)
{
    const char *hash = strstr(label, "##");
    return hash_label(hash ? hash + 2 : label);
}

void ui_attach(Ui *ui, struct UiAnim *anim, struct UiFx *fx, const TextFont *display, const TextFont *bold)
{
    ui->anim = anim;
    ui->fx = fx;
    ui->display = display;
    ui->bold = bold;
}

void ui_begin(Ui *ui, GfxContext *gfx, const TextFont *font, MgInput *in)
{
    ui->modal = false;
    ui->gfx = gfx;
    ui->font = font;
    ui->in = in;
    ui->input_off = false;
    ui->seen_count = 0;
    if (ui->active_id && (ui->active_touch < 0 || !ui->in->touches[ui->active_touch].active)) {
        /* touch ended last frame: releases are handled by the widget this frame */
        if (ui->active_touch < 0 || !ui->in->touches[ui->active_touch].ended) {
            ui->active_id = 0;
            ui->active_touch = -1;
        }
    }
}

/* Claim a touch that just began inside the rect; returns its slot or -1.
 * Backdrops (ui_block) run before the widgets on top of them, so a widget
 * may take over a touch a backdrop swallowed. */
static int claim_as(Ui *ui, float x, float y, float w, float h, int owner)
{
    if (ui->input_off) {
        return -1;
    }
    for (int i = 0; i < MG_MAX_TOUCHES; i++) {
        MgTouch *t = &ui->in->touches[i];
        bool free = t->owner == MG_TOUCH_FREE || (owner == UI_TOUCH_OWNER && t->owner == UI_BLOCK_OWNER);
        bool visible = !ui->clip_on || (t->x0 >= ui->clip[0] && t->x0 <= ui->clip[0] + ui->clip[2] &&
                                        t->y0 >= ui->clip[1] && t->y0 <= ui->clip[1] + ui->clip[3]);
        if (t->began && free && visible && inside(t, x, y, w, h, true)) {
            t->owner = owner;
            return i;
        }
    }
    return -1;
}

static int claim(Ui *ui, float x, float y, float w, float h) { return claim_as(ui, x, y, w, h, UI_TOUCH_OWNER); }

bool ui_block(Ui *ui, float x, float y, float w, float h) { return claim_as(ui, x, y, w, h, UI_BLOCK_OWNER) >= 0; }

void ui_panel(Ui *ui, float x, float y, float w, float h) { ui_card(ui, x, y, w, h); }

void ui_label(Ui *ui, float x, float y, float size, UiColor c, const char *text)
{
    text_draw(ui->gfx, ui->font, x, y, size, c, text);
}

/* Generic press/release tracking. Returns true on release inside. */
static void remember(Ui *ui, const char *label, float x, float y, float w, float h)
{
    const char *hash = strstr(label, "##");
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    bool clipped = ui->clip_on && (cx < ui->clip[0] || cx > ui->clip[0] + ui->clip[2] || cy < ui->clip[1] ||
                                   cy > ui->clip[1] + ui->clip[3]);
    if (clipped || ui->seen_count >= (int)MG_ARRAY_COUNT(ui->seen)) {
        return;
    }
    int i = ui->seen_count++;
    ui->seen[i].key = hash_label(hash ? hash + 2 : label);
    (void)hash;
    ui->seen[i].x = x;
    ui->seen[i].y = y;
    ui->seen[i].w = w;
    ui->seen[i].h = h;
    ui->seen[i].rect_only = false;
}

void ui_remember(Ui *ui, const char *id, float x, float y, float w, float h)
{
    const int before = ui->seen_count;
    remember(ui, id, x, y, w, h);
    if (ui->seen_count > before) {
        ui->seen[before].rect_only = true;
    }
}

bool ui_rect_of(const Ui *ui, const char *key, float *x, float *y, float *w, float *h)
{
    const uint32_t k = hash_label(key);
    for (int i = ui->seen_count - 1; i >= 0; i--) {
        if (ui->seen[i].key == k) {
            *x = ui->seen[i].x, *y = ui->seen[i].y, *w = ui->seen[i].w, *h = ui->seen[i].h;
            return true;
        }
    }
    return false;
}

bool ui_find(const Ui *ui, const char *key, float *x, float *y)
{
    uint32_t k = hash_label(key);
    for (int i = ui->seen_count - 1; i >= 0; i--) { /* topmost first */
        if (ui->seen[i].key == k) {
            *x = ui->seen[i].x + ui->seen[i].w * 0.5f;
            *y = ui->seen[i].y + ui->seen[i].h * 0.5f;
            return true;
        }
    }
    return false;
}

static void press_motion(Ui *ui, uint32_t key, bool down)
{
    if (!ui->anim) {
        return;
    }
    UiAnim *a = ui->anim;
    if (a->reduced) { /* a 4 % uniform press, a 150 ms way back */
        ui_anim_to(a, key, UI_SX, 1.0f, down ? k_ui.reduced_press : 1.0f, down ? k_ui.press_s : k_ui.reduced_s, UI_EASE_LINEAR, NULL, down);
        ui_anim_to(a, key, UI_SY, 1.0f, down ? k_ui.reduced_press : 1.0f, down ? k_ui.press_s : k_ui.reduced_s, UI_EASE_LINEAR, NULL, down);
    } else if (down) { /* press: (1.07, 0.86) in 90 ms, held */
        ui_anim_to(a, key, UI_SX, 1.0f, k_ui.press_sx, k_ui.press_s, UI_EASE_OUT, NULL, true);
        ui_anim_to(a, key, UI_SY, 1.0f, k_ui.press_sy, k_ui.press_s, UI_EASE_OUT, NULL, true);
    } else { /* release: the jelly wobble back to (1, 1), from wherever it is */
        ui_anim_keys(a, key, UI_SX, 1.0f, k_ui.release_t, k_ui.release_sx, 5, k_ui.release_s, UI_EASE_OUT);
        ui_anim_keys(a, key, UI_SY, 1.0f, k_ui.release_t, k_ui.release_sy, 5, k_ui.release_s, UI_EASE_OUT);
    }
}

/* Press / release tracking. The action fires on the touch-up inside, at once: motion never delays it. */
bool ui_press(Ui *ui, const char *label, float x, float y, float w, float h, bool *held, bool quiet)
{
    uint32_t id = ui_key(label);
    remember(ui, label, x, y, w, h);
    int t = claim(ui, x, y, w, h);
    if (t >= 0) {
        ui->active_id = id;
        ui->active_touch = t;
        ui->press_inside = true;
        press_motion(ui, id, true);
        if (ui->fx && !quiet) {
            ui_fx_tier(ui->fx, UI_T0, x + w * 0.5f, y + h * 0.5f, true);
        }
    }
    bool clicked = false;
    *held = false;
    if (ui->active_id == id && ui->active_touch >= 0) {
        MgTouch *tt = &ui->in->touches[ui->active_touch];
        if (tt->active) {
            *held = inside(tt, x, y, w, h, false);
            if (*held != ui->press_inside) { /* off the control: it springs back; back on: pressed again */
                ui->press_inside = *held;
                press_motion(ui, id, *held);
            }
        } else if (tt->ended) {
            clicked = inside(tt, x, y, w, h, false);
            if (ui->press_inside) {
                press_motion(ui, id, false);
            }
            ui->active_id = 0;
            ui->active_touch = -1;
            ui->press_inside = false;
        }
    }
    return clicked;
}

static bool track(Ui *ui, const char *label, float x, float y, float w, float h, bool *held) { return ui_press(ui, label, x, y, w, h, held, true); }

bool ui_hit_area(Ui *ui, const char *id, float x, float y, float w, float h, bool *held)
{
    return track(ui, id, x, y, w, h, held);
}

bool ui_button(Ui *ui, const char *label, float x, float y, float w, float h, bool highlighted)
{
    return ui_button_v(ui, label, x, y, w, h, highlighted ? UI_V_PRIMARY : UI_V_SECONDARY, 0);
}

bool ui_slider(Ui *ui, const char *label, float x, float y, float w, float *value, float min, float max,
               const char *fmt)
{
    const float label_h = 20.0f, track_h = 30.0f;
    uint32_t id = hash_label(label);
    float ty = y + label_h;
    int t = claim(ui, x - 8.0f, ty - 6.0f, w + 16.0f, track_h + 12.0f);
    if (t >= 0) {
        ui->active_id = id;
        ui->active_touch = t;
    }
    bool changed = false;
    if (ui->active_id == id && ui->active_touch >= 0) {
        MgTouch *tt = &ui->in->touches[ui->active_touch];
        float f = mg_saturate((tt->x - x) / w);
        float nv = min + (max - min) * f;
        changed = nv != *value;
        *value = nv;
        if (!tt->active) {
            ui->active_id = 0;
            ui->active_touch = -1;
        }
    }
    char vis[64], buf[96], num[32];
    visible(label, vis, sizeof(vis));
    SDL_snprintf(num, sizeof(num), fmt, (double)*value);
    SDL_snprintf(buf, sizeof(buf), "%s  %s", vis, num);
    ui_text(ui, TEXT_FACE_BOLD, x, y, k_ui.t_small, ui_rgba(k_ui.ink_soft), buf);
    float f = mg_saturate((*value - min) / (max - min));
    const float th = 10.0f, cy = ty + track_h * 0.5f;
    ui_shape(ui->gfx, &(UiShape){.x = x, .y = cy - th * 0.5f, .w = w, .h = th, .radius = -1.0f, .color = ui_rgba(k_ui.lip_mint)});
    ui_shape(ui->gfx, &(UiShape){.x = x, .y = cy - th * 0.5f, .w = mg_maxf(w * f, th), .h = th, .radius = -1.0f, .color = ui_rgba(k_ui.fill)});
    const float kr = 13.0f;
    ui_surface(ui, x + w * f - kr, cy - kr - 1.0f, 2.0f * kr, 2.0f * kr - 2.0f, -1.0f, 3.0f, ui_rgba(k_ui.primary), ui_rgba(k_ui.primary_lip), 3.0f,
               ui_rgba(k_ui.highlight));
    return changed;
}

bool ui_toggle(Ui *ui, const char *label, float x, float y, float w, bool *value)
{
    char vis[64];
    visible(label, vis, sizeof(vis));
    const float h = k_ui.toggle_h;
    ui_text(ui, TEXT_FACE_BOLD, x, y + h * 0.5f - k_ui.t_body * 0.62f, k_ui.t_body, k_ui_theme.text, vis);
    const char *hash = strstr(label, "##");
    return ui_switch(ui, hash ? hash + 2 : label, x + w - k_ui.toggle_w, y, value);
}

float ui_letter_keypad(Ui *ui, const char *id, float x, float y, float w, float key_h, char *code, int max_len)
{
    const int cols = 9;
    const float gap = 4.0f;
    float kw = (w - (float)(cols - 1) * gap) / (float)cols;
    int len = (int)strlen(code);
    for (int i = 0; i < 27; i++) {
        float kx = x + (float)(i % cols) * (kw + gap);
        float ky = y + (float)(i / cols) * (key_h + gap);
        char label[24];
        if (i < 26) {
            SDL_snprintf(label, sizeof(label), "%c##%s_%c", 'A' + i, id, 'A' + i);
            if (ui_button(ui, label, kx, ky, kw, key_h, false) && len < max_len) {
                code[len++] = (char)('A' + i);
                code[len] = 0;
            }
        } else {
            SDL_snprintf(label, sizeof(label), "<##%s_del", id);
            if (ui_button(ui, label, kx, ky, kw, key_h, false) && len > 0) {
                code[--len] = 0;
            }
        }
    }
    return 3.0f * (key_h + gap);
}

float ui_scroll_begin(Ui *ui, UiScroll *s, float x, float y, float w, float h, bool horizontal)
{
    if (s->touch > 0 && !ui->in->touches[s->touch - 1].active) {
        s->touch = 0;
    }
    for (int i = 0; !ui->input_off && s->touch == 0 && i < MG_MAX_TOUCHES; i++) {
        MgTouch *t = &ui->in->touches[i];
        if (!t->active || t->owner == UI_SCROLL_OWNER || t->x0 < x || t->x0 > x + w || t->y0 < y || t->y0 > y + h) {
            continue;
        }
        float moved = horizontal ? t->x - t->x0 : t->y - t->y0;
        float across = horizontal ? t->y - t->y0 : t->x - t->x0;
        if (fabsf(moved) > 10.0f && fabsf(moved) > fabsf(across)) {
            s->touch = i + 1;
            s->grab = s->offset + (horizontal ? t->x0 : t->y0);
            t->owner = UI_SCROLL_OWNER;
            if (ui->active_touch == i) { /* it was pressing a button: cancel that */
                ui->active_id = 0;
                ui->active_touch = -1;
            }
        }
    }
    if (s->touch > 0) {
        const MgTouch *t = &ui->in->touches[s->touch - 1];
        s->offset = s->grab - (horizontal ? t->x : t->y);
    }
    float max_off = mg_maxf(s->content - (horizontal ? w : h), 0.0f);
    s->offset = mg_clampf(s->offset, 0.0f, max_off);
    gfx_ui_clip(ui->gfx, x, y, w, h);
    ui->clip_on = true;
    ui->clip[0] = x;
    ui->clip[1] = y;
    ui->clip[2] = w;
    ui->clip[3] = h;
    return s->offset;
}

void ui_scroll_end(Ui *ui, UiScroll *s, float viewport, float content)
{
    (void)viewport;
    s->content = content;
    gfx_ui_clip_off(ui->gfx);
    ui->clip_on = false;
}

int ui_text_field(Ui *ui, const char *id, float x, float y, float w, float h, char *buf, int cap, bool *editing,
                  const char *allowed)
{
    int result = UI_EDIT_NONE;
    bool held;
    if (ui_hit_area(ui, id, x, y, w, h, &held) && !*editing) {
        *editing = true;
        result = UI_EDIT_STARTED;
    }
    if (*editing) {
        /* a new touch anywhere else ends editing */
        for (int i = 0; i < MG_MAX_TOUCHES; i++) {
            const MgTouch *t = &ui->in->touches[i];
            if (t->began && !inside(t, x, y, w, h, true)) {
                *editing = false;
                result = UI_EDIT_DONE;
            }
        }
        size_t len = strlen(buf);
        for (const char *c = ui->in->text; *editing && *c; c++) {
            if (strchr(allowed, *c) && len + 1 < (size_t)cap) {
                buf[len++] = *c;
                buf[len] = 0;
                result = UI_EDIT_CHANGED;
            }
        }
        if (*editing && ui->in->key_pressed[SDL_SCANCODE_BACKSPACE] && len > 0) {
            buf[--len] = 0;
            result = UI_EDIT_CHANGED;
        }
        if (*editing && (ui->in->key_pressed[SDL_SCANCODE_RETURN] || ui->in->key_pressed[SDL_SCANCODE_KP_ENTER])) {
            *editing = false;
            result = UI_EDIT_DONE;
        }
    }
    ui_surface(ui, x, y, w, h - k_ui.lip_row, k_ui.r_row, k_ui.lip_row, ui_rgba(k_ui.surface), ui_rgba(k_ui.lip_mint), 0.0f, ui_rgba(0));
    if (*editing) { /* the focus ring */
        ui_shape(ui->gfx, &(UiShape){.x = x - 1.5f, .y = y - 1.5f, .w = w + 3.0f, .h = h - k_ui.lip_row + 3.0f, .radius = k_ui.r_row + 1.5f, .stroke = k_ui.ring_width,
                                     .color = ui_rgba(k_ui.teal_text)});
    }
    float size = mg_minf(18.0f, h * 0.5f);
    float tw = text_draw(ui->gfx, ui->font, x + 12.0f, y + (h - size * 1.2f) * 0.5f, size, k_ui_theme.text, buf);
    if (*editing) { /* caret */
        ui_rect(ui->gfx, ui->font, x + 13.0f + tw, y + h * 0.25f, 2.0f, h * 0.5f, k_ui_theme.text);
    }
    return result;
}
