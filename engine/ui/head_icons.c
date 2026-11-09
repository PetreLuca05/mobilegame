#include "head_icons.h"

#include <string.h>

#define COLS 8
#define CAM_DIST 1.6f  /* hair and accessories stay inside the circle */
#define CAM_FOV 28.0f

/* suit is part of the key by definition, though the head does not show it */
static uint32_t skin_hash(const NetSkin *s)
{
    const uint8_t b[6] = {s->body, s->skin, s->hair, s->hair_color, s->suit, s->accessory};
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

void head_icons_init(HeadIcons *h, void (*draw)(void *, GfxContext *, const NetSkin *),
                     uint32_t (*skin_rgb)(void *, const NetSkin *), void *user)
{
    memset(h, 0, sizeof(*h));
    h->draw = draw;
    h->skin_rgb = skin_rgb;
    h->user = user;
    h->frame = 1;
}

void head_icons_clear(HeadIcons *h)
{
    for (int i = 0; i < HEAD_ICON_CELLS; i++) {
        h->cell[i].state = 0;
    }
    h->queue_count = 0;
}

/* A free cell, else the longest unused: never one drawn this frame or waiting to render */
static int take_cell(const HeadIcons *h)
{
    int best = -1;
    for (int i = 0; i < HEAD_ICON_CELLS; i++) {
        if (h->cell[i].state == 0) {
            return i;
        }
        if (h->cell[i].state == 2 && h->cell[i].last_used != h->frame &&
            (best < 0 || h->cell[i].last_used < h->cell[best].last_used)) {
            best = i;
        }
    }
    return best;
}

HeadUv head_icon_get(HeadIcons *h, const NetSkin *skin)
{
    const uint32_t key = skin_hash(skin);
    int c = -1;
    for (int i = 0; i < HEAD_ICON_CELLS && c < 0; i++) {
        c = h->cell[i].state != 0 && h->cell[i].hash == key ? i : -1;
    }
    if (c < 0) {
        c = take_cell(h);
        if (c < 0 || h->queue_count == HEAD_ICON_CELLS) {
            return (HeadUv){0};
        }
        h->cell[c].hash = key;
        h->cell[c].skin = *skin;
        h->cell[c].state = 1;
        h->cell[c].waited = 0;
        h->queue[h->queue_count++] = (uint8_t)c;
    }
    h->cell[c].last_used = h->frame;
    if (h->cell[c].state != 2) {
        h->cell[c].waited++;
        if (h->cell[c].waited > h->most_placeholder_frames) {
            h->most_placeholder_frames = h->cell[c].waited;
        }
        return (HeadUv){0};
    }
    const float s = 1.0f / (float)COLS;
    const float u = (float)(c % COLS) * s, v = (float)(c / COLS) * s;
    return (HeadUv){true, u, v, u + s, v + s};
}

void head_icon_draw(HeadIcons *h, GfxContext *g, const TextFont *f, const NetSkin *skin, const char *name, float cx, float cy,
                    float size, bool faded)
{
    const HeadUv uv = head_icon_get(h, skin);
    const float r = size * 0.5f;
    const uint8_t alpha = faded ? 128 : 255;
    if (uv.ready) {
        const UiColor ring = {255, 255, 255, alpha};
        ui_textured_quad(g, cx - r, cy - r, cx + r, cy + r, uv.u0 + 2.0f, uv.v0, uv.u1 + 2.0f, uv.v1, ring);
        return;
    }
    const uint32_t rgb = h->skin_rgb ? h->skin_rgb(h->user, skin) : 0xBBBBBBu;
    ui_circle(g, f, cx, cy, r, (UiColor){(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, alpha});
    char first[5] = {0};
    if (name && name[0]) { /* one UTF-8 character */
        int n = 1;
        while (n < 4 && (((unsigned char)name[n]) & 0xC0) == 0x80) {
            n++;
        }
        memcpy(first, name, (size_t)n);
    }
    text_draw_centered(g, f, cx, cy, size * 0.5f, (UiColor){32, 36, 44, alpha}, first);
}

float head_chip_width(const TextFont *f, const char *name) { return 4.0f + 36.0f + 8.0f + text_width(f, name, 16.0f) + 14.0f; }

void head_chip_draw(HeadIcons *h, GfxContext *g, const TextFont *f, const NetSkin *skin, const char *name, float right, float y)
{
    const float head = 36.0f, pad = 4.0f, size = 16.0f;
    const float w = head_chip_width(f, name), x = right - w;
    ui_round_rect(g, f, x, y, w, head + 2.0f * pad, 22.0f, ui_rgba(0x1E2230B0));
    head_icon_draw(h, g, f, skin, name, x + pad + head * 0.5f, y + pad + head * 0.5f, head, false);
    text_draw(g, f, x + pad + head + 8.0f, y + pad + head * 0.5f - size * 0.65f, size, ui_rgba(0xFFFFFFFF), name);
}

int head_icons_used(const HeadIcons *h)
{
    int n = 0;
    for (int i = 0; i < HEAD_ICON_CELLS; i++) {
        n += h->cell[i].state != 0 ? 1 : 0;
    }
    return n;
}

void head_icons_update(HeadIcons *h, GfxContext *g)
{
    h->frame++;
    if (h->queue_count == 0) {
        return;
    }
    const HMM_Vec3 eye = HMM_V3(0.0f, 0.0f, CAM_DIST);
    const HMM_Mat4 vp = HMM_MulM4(HMM_Perspective_RH_ZO(HMM_AngleDeg(CAM_FOV), 1.0f, 0.2f, 12.0f),
                                  HMM_LookAt_RH(eye, HMM_V3(0, 0, 0), HMM_V3(0, 1, 0)));
    const int n = h->queue_count < GFX_HEADS_PER_FRAME ? h->queue_count : GFX_HEADS_PER_FRAME;
    for (int i = 0; i < n; i++) {
        const int c = h->queue[i];
        gfx_head_cell_begin(g, c, &vp, eye);
        h->draw(h->user, g, &h->cell[c].skin);
        gfx_head_cell_end(g);
        h->cell[c].state = 2;
        h->generated++;
    }
    h->queue_count -= n;
    memmove(h->queue, h->queue + n, (size_t)h->queue_count);
}
