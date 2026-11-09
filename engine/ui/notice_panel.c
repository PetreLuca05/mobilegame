#include "notice_panel.h"

#include <SDL3/SDL.h>
#include <string.h>

#define SLIDE_S 0.18f
#define GAP_TOP 8.0f
#define PANEL_W 360.0f
#define PANEL_H 64.0f
#define HEAD_SIZE 44.0f
#define BUTTON_W 82.0f

void notice_push(NoticePanel *p, NoticeKind kind, const char *id, const char *name, const NetSkin *skin, const char *line, double duration)
{
    for (int i = 0; i < p->count; i++) {
        if (p->q[i].kind == kind && strcmp(p->q[i].id, id) == 0) {
            return; /* already waiting */
        }
    }
    /* someone waiting at the door goes before anything that can wait in the Requests tab */
    int at = p->count;
    if (kind == NOTICE_JOIN || kind == NOTICE_INVITE) {
        at = p->count > 0 ? 1 : 0;
        while (at < p->count && (p->q[at].kind == NOTICE_JOIN || p->q[at].kind == NOTICE_INVITE)) {
            at++;
        }
        if (p->count == NOTICE_MAX && at < NOTICE_MAX) {
            p->count--; /* the last friend notice; the request itself stays in the list */
        }
        if (p->count > 0 && p->q[0].kind != NOTICE_JOIN && p->q[0].kind != NOTICE_INVITE) {
            p->leaving = true;
        }
    }
    if (p->count == NOTICE_MAX) {
        return;
    }
    memmove(&p->q[at + 1], &p->q[at], (size_t)(p->count - at) * sizeof(Notice));
    p->count++;
    Notice *n = &p->q[at];
    memset(n, 0, sizeof(*n));
    n->kind = kind;
    SDL_snprintf(n->id, sizeof(n->id), "%s", id);
    SDL_snprintf(n->name, sizeof(n->name), "%s", name);
    SDL_snprintf(n->line, sizeof(n->line), "%s", line);
    n->skin = *skin;
    n->duration = duration;
}

static void pop_front(NoticePanel *p)
{
    memmove(&p->q[0], &p->q[1], (size_t)(p->count - 1) * sizeof(Notice));
    p->count--;
    p->slide = 0.0f;
    p->leaving = false;
    p->started = false;
}

void notice_remove(NoticePanel *p, const char *id)
{
    for (int i = 0; i < p->count; i++) {
        if (strcmp(p->q[i].id, id) != 0) {
            continue;
        }
        if (i == 0) {
            p->leaving = true;
        } else {
            memmove(&p->q[i], &p->q[i + 1], (size_t)(p->count - i - 1) * sizeof(Notice));
            p->count--;
        }
        return;
    }
}

NoticeResult notice_panel_draw(NoticePanel *p, Ui *ui, HeadIcons *h, const ui_frame_t *f, double now, float dt, const char *accept,
                               const char *decline, NoticeKind *kind, char id[NET_ID_LEN])
{
    p->just_shown = false;
    if (p->count == 0) {
        return NOTICE_NONE;
    }
    Notice *n = &p->q[0];
    if (!p->started) {
        p->started = true;
        p->just_shown = true;
        n->expires = now + n->duration;
    }
    if (!p->leaving && now >= n->expires) {
        p->leaving = true; /* whoever owns the request times it out too */
    }
    p->slide = p->leaving ? mg_maxf(p->slide - dt / SLIDE_S, 0.0f) : mg_minf(p->slide + dt / SLIDE_S, 1.0f);
    if (p->leaving && p->slide <= 0.0f) {
        pop_front(p);
        return NOTICE_NONE;
    }
    const float k = f->h >= 600.0f ? 1.3f : 1.0f;
    const float ease = 1.0f - (1.0f - p->slide) * (1.0f - p->slide);
    const float w = PANEL_W * k, ph = PANEL_H * k, x = (f->w - w) * 0.5f;
    const float rest = ui_safe_top(f) + GAP_TOP, y = -ph + (rest + ph) * ease;
    p->x = x;
    p->y = rest;
    p->w = w;
    p->h = ph;
    ui_block(ui, x, y, w, ph);
    ui_round_rect(ui->gfx, ui->font, x, y, w, ph, 14.0f, ui_rgba(0x1E2230F8));
    const float head = HEAD_SIZE * k, pad = (ph - head) * 0.5f;
    head_icon_draw(h, ui->gfx, ui->font, &n->skin, n->name, x + pad + head * 0.5f, y + ph * 0.5f, head, false);
    const bool buttons = n->kind != NOTICE_INFO;
    const float bw = BUTTON_W * k, bh = 44.0f, tx = x + pad + head + 10.0f;
    const float avail = w - (tx - x) - (buttons ? 2.0f * bw + 18.0f : pad);
    text_draw(ui->gfx, ui->font, tx, y + ph * 0.5f - 21.0f * k, 15.0f * k, ui_rgba(0xFFFFFFFF), n->name);
    /* fits the message on its line whatever the language */
    float size = 12.0f * k;
    const float tw = text_width(ui->font, n->line, size);
    size = tw > avail ? size * avail / tw : size;
    text_draw(ui->gfx, ui->font, tx, y + ph * 0.5f + 2.0f * k, size, ui_rgba(0xFFFFFFB3), n->line);
    NoticeResult res = NOTICE_NONE;
    if (buttons && !p->leaving) {
        char label[64];
        const float by = y + (ph - bh) * 0.5f;
        SDL_snprintf(label, sizeof(label), "%s##notice_ok", accept);
        if (ui_button(ui, label, x + w - 2.0f * bw - 14.0f, by, bw, bh, true)) {
            res = NOTICE_ACCEPT;
        }
        SDL_snprintf(label, sizeof(label), "%s##notice_no", decline);
        if (ui_button(ui, label, x + w - bw - 8.0f, by, bw, bh, false)) {
            res = NOTICE_DECLINE;
        }
    }
    const float left = (float)((n->expires - now) / n->duration);
    ui_rect(ui->gfx, ui->font, x + 14.0f, y + ph - 4.0f, (w - 28.0f) * mg_clampf(left, 0.0f, 1.0f), 2.0f, ui_rgba(0x4A8BFFFF));
    if (res != NOTICE_NONE) {
        *kind = n->kind;
        SDL_snprintf(id, NET_ID_LEN, "%s", n->id);
        p->leaving = true;
    }
    return res;
}
