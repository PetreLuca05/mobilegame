#include "account_link.h"

#include <SDL3/SDL.h>
#include <string.h>

#include "strings.h"

#define LOAD_TIMEOUT_S 20.0

void account_link_init(AccountLink *l, NetTransport *net)
{
    memset(l, 0, sizeof(*l));
    l->net = net;
}

bool account_link_active(const AccountLink *l) { return l->state != AL_IDLE; }

void account_link_begin(AccountLink *l, const char *provider, const char *id_token, const char *nonce)
{
    SDL_snprintf(l->provider, sizeof(l->provider), "%s", provider);
    l->state = AL_LINKING;
    l->failed = NULL;
    net_link_idp(l->net, provider, id_token, nonce);
}

void account_link_on_event(AccountLink *l, const NetEvent *e)
{
    if (e->type == NET_EV_PROFILE && l->state == AL_LOADING) {
        l->loaded = e->profile;
        l->state = AL_RESULT;
        return;
    }
    if (e->type != NET_EV_LINK) {
        return;
    }
    switch ((NetLinkEvent)e->value) {
    case NET_LINK_LINKED:
        l->state = AL_IDLE;
        l->linked = true;
        break;
    case NET_LINK_CONFLICT:
        SDL_snprintf(l->identity_name, sizeof(l->identity_name), "%s", e->text);
        l->state = AL_DIALOG;
        break;
    case NET_LINK_STEP:
        l->merge_step = (int)e->len;
        break;
    case NET_LINK_MERGED:
        l->state = AL_LOADING;
        l->load_until = 0.0;
        l->merged = true;
        break;
    case NET_LINK_FAILED:
        l->state = AL_IDLE;
        l->failed = "link_failed";
        break;
    }
}

static void spinner(Ui *ui, float cx, float cy, double now)
{
    for (int i = 0; i < 8; i++) {
        const float a = (float)i * 2.0f * HMM_PI32 / 8.0f;
        const float phase = SDL_fmodf((float)now * 1.2f + (float)i / 8.0f, 1.0f);
        const UiColor c = {255, 255, 255, (uint8_t)(255.0f * phase)};
        ui_circle(ui->gfx, ui->font, cx + SDL_cosf(a) * 16.0f, cy + SDL_sinf(a) * 16.0f, 3.5f, c);
    }
}

static const char *provider_label(const AccountLink *l) { return strcmp(l->provider, "apple") == 0 ? "Apple" : "Google"; }

void account_link_draw(AccountLink *l, Ui *ui, HeadIcons *h, MgApp *app, double now)
{
    if (l->state == AL_IDLE) {
        return;
    }
    const float w = app->width_pt, ph = app->height_pt;
    ui_block(ui, 0, 0, w, ph);
    ui_rect(ui->gfx, ui->font, 0, 0, w, ph, ui_rgba(0x000000A0));
    char label[96];
    if (l->state == AL_LINKING || l->state == AL_MERGING || l->state == AL_LOADING) {
        if (l->state == AL_LOADING) {
            if (l->load_until == 0.0) {
                l->load_until = now + LOAD_TIMEOUT_S;
            } else if (now > l->load_until) { /* the account is on this device; the network thread keeps loading it */
                l->state = AL_IDLE;
                l->failed = "link_load_slow";
                return;
            }
        }
        const bool loading = l->state == AL_LOADING || (l->state == AL_MERGING && l->merge_step >= 3);
        spinner(ui, w * 0.5f, ph * 0.5f - 18.0f, now);
        text_draw_centered(ui->gfx, ui->font, w * 0.5f, ph * 0.5f + 28.0f, 17.0f, ui_rgba(0xFFFFFFFF), str(loading ? "link_step_load" : "link_step_connect"));
        return;
    }
    const float dw = mg_minf(440.0f, w - 48.0f), dx = (w - dw) * 0.5f;
    if (l->state == AL_DIALOG) {
        const float dh = 196.0f, dy = (ph - dh) * 0.5f, inner = dw - 40.0f;
        ui_panel(ui, dx, dy, dw, dh);
        char body[220];
        SDL_snprintf(body, sizeof(body), str("link_dialog"), provider_label(l), l->identity_name[0] ? l->identity_name : provider_label(l));
        /* wrapped by hand: the font helpers draw single lines */
        float y = dy + 20.0f;
        const char *p = body;
        while (*p) {
            char line[120];
            int best = 0, n = 0;
            while (p[n] && n < (int)sizeof(line) - 1) {
                line[n] = p[n];
                line[n + 1] = 0;
                if (p[n] == ' ' || !p[n + 1]) {
                    if (text_width(ui->font, line, 15.0f) > inner) {
                        break;
                    }
                    best = n + 1;
                }
                n++;
            }
            if (best == 0) {
                best = n ? n : 1;
            }
            SDL_snprintf(line, sizeof(line), "%.*s", best, p);
            text_draw(ui->gfx, ui->font, dx + 20.0f, y, 15.0f, ui_rgba(0xFFFFFFFF), line);
            y += 22.0f;
            p += best;
        }
        const float bw = (dw - 48.0f) * 0.5f, by = dy + dh - 62.0f;
        SDL_snprintf(label, sizeof(label), "%s##link_cancel", str("link_cancel"));
        if (ui_button(ui, label, dx + 16.0f, by, bw, 48.0f, false)) {
            net_link_cancel(l->net);
            l->state = AL_IDLE;
        }
        SDL_snprintf(label, sizeof(label), "%s##link_continue", str("link_continue"));
        if (ui_button(ui, label, dx + 32.0f + bw, by, bw, 48.0f, true)) {
            net_link_merge(l->net);
            l->merge_step = 0;
            l->state = AL_MERGING;
        }
        return;
    }
    /* AL_RESULT */
    const float dh = 230.0f, dy = (ph - dh) * 0.5f;
    ui_panel(ui, dx, dy, dw, dh);
    head_icon_draw(h, ui->gfx, ui->font, &l->loaded.skin, l->loaded.display_name, dx + dw * 0.5f, dy + 50.0f, 64.0f, false);
    SDL_snprintf(label, sizeof(label), str("link_welcome_back"), l->loaded.display_name);
    text_draw_centered(ui->gfx, ui->font, dx + dw * 0.5f, dy + 104.0f, 20.0f, ui_rgba(0xFFFFFFFF), label);
    SDL_snprintf(label, sizeof(label), str("link_summary"), l->loaded.stats.sessions, l->loaded.stats.play_seconds / 60);
    text_draw_centered(ui->gfx, ui->font, dx + dw * 0.5f, dy + 134.0f, 14.0f, ui_rgba(0xFFFFFFB3), label);
    SDL_snprintf(label, sizeof(label), "%s##link_done", str("done"));
    if (ui_button(ui, label, dx + 20.0f, dy + dh - 62.0f, dw - 40.0f, 48.0f, true)) {
        l->state = AL_IDLE;
    }
}
