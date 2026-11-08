#include "widgets.h"

#include <string.h>

const UiTheme k_ui_theme = {
    .panel = {22, 26, 38, 232},
    .button = {255, 255, 255, 34},
    .button_down = {255, 255, 255, 90},
    .accent = {74, 139, 255, 255},
    .text = {255, 255, 255, 255},
    .text_dim = {255, 255, 255, 160},
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

void ui_begin(Ui *ui, GfxContext *gfx, const TextFont *font, MgInput *in)
{
    ui->gfx = gfx;
    ui->font = font;
    ui->in = in;
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
    for (int i = 0; i < MG_MAX_TOUCHES; i++) {
        MgTouch *t = &ui->in->touches[i];
        bool free = t->owner == MG_TOUCH_FREE || (owner == UI_TOUCH_OWNER && t->owner == UI_BLOCK_OWNER);
        if (t->began && free && inside(t, x, y, w, h, true)) {
            t->owner = owner;
            return i;
        }
    }
    return -1;
}

static int claim(Ui *ui, float x, float y, float w, float h) { return claim_as(ui, x, y, w, h, UI_TOUCH_OWNER); }

bool ui_block(Ui *ui, float x, float y, float w, float h) { return claim_as(ui, x, y, w, h, UI_BLOCK_OWNER) >= 0; }

void ui_panel(Ui *ui, float x, float y, float w, float h)
{
    ui_block(ui, x, y, w, h);
    ui_round_rect(ui->gfx, ui->font, x, y, w, h, 16.0f, k_ui_theme.panel);
}

void ui_label(Ui *ui, float x, float y, float size, UiColor c, const char *text)
{
    text_draw(ui->gfx, ui->font, x, y, size, c, text);
}

/* Generic press/release tracking. Returns true on release inside. */
static bool track(Ui *ui, uint32_t id, float x, float y, float w, float h, bool *held)
{
    int t = claim(ui, x, y, w, h);
    if (t >= 0) {
        ui->active_id = id;
        ui->active_touch = t;
    }
    bool clicked = false;
    *held = false;
    if (ui->active_id == id && ui->active_touch >= 0) {
        MgTouch *tt = &ui->in->touches[ui->active_touch];
        if (tt->active) {
            *held = inside(tt, x, y, w, h, false);
        } else if (tt->ended) {
            clicked = inside(tt, x, y, w, h, false);
            ui->active_id = 0;
            ui->active_touch = -1;
        }
    }
    return clicked;
}

bool ui_hit_area(Ui *ui, const char *id, float x, float y, float w, float h, bool *held)
{
    return track(ui, hash_label(id), x, y, w, h, held);
}

bool ui_button(Ui *ui, const char *label, float x, float y, float w, float h, bool highlighted)
{
    bool held;
    bool clicked = track(ui, hash_label(label), x, y, w, h, &held);
    UiColor bg = held ? k_ui_theme.button_down : (highlighted ? k_ui_theme.accent : k_ui_theme.button);
    ui_round_rect(ui->gfx, ui->font, x, y, w, h, mg_minf(12.0f, h * 0.5f), bg);
    char vis[64];
    visible(label, vis, sizeof(vis));
    float size = mg_minf(18.0f, h * 0.45f);
    text_draw_centered(ui->gfx, ui->font, x + w * 0.5f, y + h * 0.5f, size, k_ui_theme.text, vis);
    return clicked;
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
    text_draw(ui->gfx, ui->font, x, y, 15.0f, k_ui_theme.text_dim, buf);
    float f = mg_saturate((*value - min) / (max - min));
    ui_round_rect(ui->gfx, ui->font, x, ty + track_h * 0.5f - 4.0f, w, 8.0f, 4.0f, k_ui_theme.button);
    ui_round_rect(ui->gfx, ui->font, x, ty + track_h * 0.5f - 4.0f, w * f, 8.0f, 4.0f, k_ui_theme.accent);
    ui_circle(ui->gfx, ui->font, x + w * f, ty + track_h * 0.5f, 12.0f, k_ui_theme.text);
    return changed;
}

bool ui_toggle(Ui *ui, const char *label, float x, float y, float w, bool *value)
{
    const float h = 36.0f;
    bool held;
    bool clicked = track(ui, hash_label(label), x, y, w, h, &held);
    if (clicked) {
        *value = !*value;
    }
    char vis[64];
    visible(label, vis, sizeof(vis));
    text_draw(ui->gfx, ui->font, x, y + 8.0f, 16.0f, k_ui_theme.text, vis);
    float sw = 52.0f, sh = 30.0f, sx = x + w - sw, sy = y + (h - sh) * 0.5f;
    ui_round_rect(ui->gfx, ui->font, sx, sy, sw, sh, sh * 0.5f, *value ? k_ui_theme.accent : k_ui_theme.button);
    ui_circle(ui->gfx, ui->font, *value ? sx + sw - sh * 0.5f : sx + sh * 0.5f, sy + sh * 0.5f, sh * 0.5f - 3.0f,
              k_ui_theme.text);
    return clicked;
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
            SDL_snprintf(label, sizeof(label), "%c##%s", 'A' + i, id);
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
