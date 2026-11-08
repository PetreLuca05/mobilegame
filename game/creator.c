#include "creator.h"

#include <SDL3/SDL_stdinc.h>

#include "avatar.h"
#include "strings.h"

#define TILE 56.0f

static UiColor rgb(uint32_t hex) { return ui_rgba((hex << 8) | 0xFFu); }

typedef enum { ROW_TEXT, ROW_COLOR, ROW_SUIT } RowKind;

/* one sideways row of tiles; returns the index tapped or -1 */
static int row(Creator *c, Ui *ui, int r, RowKind kind, const char *title, float x, float y, float w, int count,
               int selected, const uint32_t *colors, const char *(*name)(int))
{
    ui_label(ui, x, y, 15.0f, k_ui_theme.text_dim, title);
    y += 22.0f;
    float tw = kind == ROW_TEXT ? 96.0f : (kind == ROW_SUIT ? 84.0f : TILE);
    float off = ui_scroll_begin(ui, &c->rows[r], x - 4.0f, y - 4.0f, w + 8.0f, TILE + 8.0f, true);
    int picked = -1;
    for (int i = 0; i < count; i++) {
        float tx = x + (float)i * (tw + 10.0f) - off;
        char id[24];
        SDL_snprintf(id, sizeof(id), "cr%d_%d_%d", c->tab, r, i);
        bool held = false;
        if (ui_hit_area(ui, id, tx, y, tw, TILE, &held)) {
            picked = i;
        }
        if (i == selected) {
            ui_round_rect(ui->gfx, ui->font, tx - 3.0f, y - 3.0f, tw + 6.0f, TILE + 6.0f, 15.0f, k_ui_theme.accent);
        }
        ui_round_rect(ui->gfx, ui->font, tx, y, tw, TILE, 12.0f, held ? k_ui_theme.button_down : ui_rgba(0x2A3042FF));
        if (kind == ROW_COLOR) {
            ui_circle(ui->gfx, ui->font, tx + tw * 0.5f, y + TILE * 0.5f, 18.0f, rgb(colors[i]));
        } else if (kind == ROW_SUIT) {
            uint32_t outfit, accent;
            avatar_suit_colors(i, &outfit, &accent);
            ui_round_rect(ui->gfx, ui->font, tx + tw * 0.5f - 14.0f, y + 8.0f, 28.0f, 30.0f, 10.0f, rgb(outfit));
            ui_circle(ui->gfx, ui->font, tx + tw * 0.5f + 6.0f, y + 18.0f, 4.0f, rgb(accent));
            text_draw_centered(ui->gfx, ui->font, tx + tw * 0.5f, y + 47.0f, 10.0f, k_ui_theme.text_dim,
                               avatar_suit_name(i));
        } else {
            text_draw_centered(ui->gfx, ui->font, tx + tw * 0.5f, y + TILE * 0.5f, 15.0f, k_ui_theme.text, name(i));
        }
    }
    ui_scroll_end(ui, &c->rows[r], w, (float)count * (tw + 10.0f));
    return picked;
}

static const char *body_name(int i) { return str(i == 0 ? "male" : "female"); }

bool creator_ui(Creator *c, Ui *ui, NetSkin *skin, float x, float y, float w, float h)
{
    (void)h;
    static const char *const tabs[5] = {"tab_body", "tab_skin", "tab_hair", "tab_suit", "tab_extras"};
    float tw = (w - 4.0f * 6.0f) / 5.0f;
    for (int i = 0; i < 5; i++) {
        char label[48];
        SDL_snprintf(label, sizeof(label), "%s##crtab%d", str(tabs[i]), i);
        if (ui_button(ui, label, x + (float)i * (tw + 6.0f), y, tw, 38.0f, c->tab == i)) {
            if (c->tab != i) {
                c->tab = i;
                c->rows[0].offset = c->rows[1].offset = 0.0f;
            }
        }
    }
    y += 52.0f;
    int p = -1, q = -1;
    NetSkin before = *skin;
    switch (c->tab) {
    case 0:
        p = row(c, ui, 0, ROW_TEXT, str("tab_body"), x, y, w, 2, skin->body, NULL, body_name);
        if (p >= 0) {
            skin->body = (uint8_t)p;
        }
        break;
    case 1:
        p = row(c, ui, 0, ROW_COLOR, str("tab_skin"), x, y, w, AVATAR_SKIN_COLORS, skin->skin, avatar_skin_colors(),
                NULL);
        if (p >= 0) {
            skin->skin = (uint8_t)p;
        }
        break;
    case 2:
        p = row(c, ui, 0, ROW_TEXT, str("hair_style"), x, y, w, AVATAR_HAIRS, skin->hair, NULL, avatar_hair_name);
        q = row(c, ui, 1, ROW_COLOR, str("hair_colour"), x, y + 96.0f, w, AVATAR_HAIR_COLORS, skin->hair_color,
                avatar_hair_colors(), NULL);
        if (p >= 0) {
            skin->hair = (uint8_t)p;
        }
        if (q >= 0) {
            skin->hair_color = (uint8_t)q;
        }
        break;
    case 3:
        p = row(c, ui, 0, ROW_SUIT, str("tab_suit"), x, y, w, AVATAR_SUITS, skin->suit, NULL, NULL);
        if (p >= 0) {
            skin->suit = (uint8_t)p;
        }
        break;
    default:
        p = row(c, ui, 0, ROW_TEXT, str("tab_extras"), x, y, w, AVATAR_ACCESSORIES, skin->accessory, NULL,
                avatar_accessory_name);
        if (p >= 0) {
            skin->accessory = (uint8_t)p;
        }
        break;
    }
    return SDL_memcmp(&before, skin, sizeof(before)) != 0;
}

void creator_randomise(NetSkin *s)
{
    s->body = (uint8_t)SDL_rand(2);
    s->skin = (uint8_t)SDL_rand(AVATAR_SKIN_COLORS);
    s->hair = (uint8_t)SDL_rand(AVATAR_HAIRS);
    s->hair_color = (uint8_t)SDL_rand(AVATAR_HAIR_COLORS);
    s->suit = (uint8_t)SDL_rand(AVATAR_SUITS);
    s->accessory = (uint8_t)SDL_rand(AVATAR_ACCESSORIES);
}
