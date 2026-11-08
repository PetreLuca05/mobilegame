#include "frontend.h"

#include <SDL3/SDL_stdinc.h>
#include <string.h>

#include "gfx/camera.h"
#include "gfx/material.h"
#include "gfx/mesh.h"
#include "modes.h"
#include "strings.h"

#define ROW_H 52.0f
#define PAD 16.0f
#define NAME_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _"

static const UiColor k_row = {0, 0, 0, 89};          /* rgba(0,0,0,0.35) */
static const UiColor k_row_hi = {255, 255, 255, 46}; /* rgba(255,255,255,0.18) */

/* ------------------------------------------------------------------ setup */

bool frontend_init(Frontend *f, GfxContext *g, Arena *scratch)
{
    memset(f, 0, sizeof(*f));
    f->pressed_row = -1;
    size_t mark = arena_mark(scratch);
    MeshBuilder mb;
    if (!mb_init(&mb, scratch, 4096, 8192)) {
        return false;
    }
    /* locker room: 6 x 6 m, floor, three walls in the backdrop colour */
    const HMM_Vec4 wall = mg_rgb(0x2A2F3A), floor = mg_rgb(0x3A404D), trim = mg_rgb(0x4A8BFF);
    mb_box(&mb, HMM_Translate(HMM_V3(0, -0.1f, 0)), HMM_V3(3.0f, 0.1f, 3.0f), floor);
    mb_box(&mb, HMM_Translate(HMM_V3(0, 1.75f, 3.1f)), HMM_V3(3.0f, 1.75f, 0.1f), wall);
    mb_box(&mb, HMM_Translate(HMM_V3(3.1f, 1.75f, 0)), HMM_V3(0.1f, 1.75f, 3.0f), wall);
    mb_box(&mb, HMM_Translate(HMM_V3(-3.1f, 1.75f, 0)), HMM_V3(0.1f, 1.75f, 3.0f), wall);
    mb_box(&mb, HMM_Translate(HMM_V3(0, 0.12f, 2.99f)), HMM_V3(3.0f, 0.02f, 0.02f), trim);
    f->room = mb_upload(&mb, g);
    mb_reset(&mb);
    mb_cylinder(&mb, HMM_M4D(1.0f), 0.75f, 0.08f, 48, mg_rgb(0x5A6273));
    f->disc = mb_upload(&mb, g);
    arena_reset_to(scratch, mark);
    return f->room != GFX_MESH_NONE && f->disc != GFX_MESH_NONE;
}

void frontend_go(Frontend *f, FeState s)
{
    if (s == f->state) {
        return;
    }
    if (f->depth < (int)MG_ARRAY_COUNT(f->stack)) {
        f->stack[f->depth++] = f->state;
    }
    f->state = s;
    f->editing_name = false;
}

FeAction frontend_back(Frontend *f)
{
    if (f->confirm_new) {
        f->confirm_new = false;
        return FE_NONE;
    }
    if (f->state == FE_WELCOME || f->state == FE_MAIN_MENU || f->depth == 0) {
        return f->state == FE_MAIN_MENU ? FE_NONE : FE_EXIT;
    }
    f->state = f->stack[--f->depth];
    f->editing_name = false;
    return FE_NONE;
}

void frontend_toast(Frontend *f, const char *msg, double now)
{
    SDL_snprintf(f->toast, sizeof(f->toast), "%s", msg);
    f->toast_until = now + 3.0;
}

/* ------------------------------------------------------------------ scene */

/* Locker room with the avatar on its turntable. `side` puts the avatar at
 * that horizontal screen position (-0.5 left half, +0.5 right half). */
static void draw_scene(Frontend *f, MgApp *app, const FrontendCtx *ctx, const AvatarMeshes *meshes, float side)
{
    GfxContext *g = app->gfx;
    float aspect = app->width_pt / app->height_pt;
    const float fov = HMM_AngleDeg(40.0f), dist = 5.2f;
    float tan_x = HMM_TanF(fov * 0.5f) * aspect;
    float tx = side * tan_x * dist; /* +X shows on the left from this side */
    GfxSceneParams sp = {0};
    sp.eye = HMM_V3(tx, 1.75f, -dist);
    sp.view = HMM_LookAt_RH(sp.eye, HMM_V3(tx, 1.3f, 0.0f), HMM_V3(0, 1, 0));
    sp.proj = camera_projection(fov, aspect, 0.05f, 40.0f, 0.0f);
    sp.sun_dir = HMM_NormV3(HMM_V3(-0.3f, 0.8f, -0.6f)); /* soft fill from the front */
    sp.sun_color = HMM_V3(0.16f, 0.16f, 0.18f);
    sp.ambient = HMM_V3(0.42f, 0.44f, 0.5f);
    sp.sky_horizon = mg_rgb(0x2A2F3A);
    sp.sky_zenith = mg_rgb(0x1E222B);
    sp.time = (float)app->time;
    gfx_set_scene(g, &sp);

    GfxLight spot = {GFX_LIGHT_SPOT,
                     HMM_V3(0.0f, 3.3f, -1.3f),
                     HMM_NormV3(HMM_V3(0.0f, -1.0f, 0.42f)),
                     7.0f,
                     HMM_V3(1.0f, 0.95f, 0.88f),
                     1.8f,
                     HMM_CosF(HMM_AngleDeg(22.0f)),
                     HMM_CosF(HMM_AngleDeg(32.0f)),
                     true,
                     900};
    gfx_add_light(g, &spot);
    HMM_Mat4 id = HMM_M4D(1.0f);
    GfxMaterial matte = mat_toon(HMM_V4(1, 1, 1, 1), 0.15f);
    gfx_draw(g, f->room, &id, &matte, 0);
    GfxMaterial gloss = mat_toon_glossy(HMM_V4(1, 1, 1, 1), 0.3f, 1.0f);
    gfx_draw(g, f->disc, &id, &gloss, 0);

    float dt = mg_minf(app->frame_dt, 0.1f);
    f->yaw += HMM_AngleDeg(6.0f) * dt + f->spin_vel * dt;
    f->spin_vel *= 1.0f - mg_minf(dt * 3.0f, 1.0f);
    AvatarPose pose = {HMM_V3(0, 0.08f, 0), f->yaw, HMM_V3(0, 0, 0), true, false, false, false};
    avatar_animate(&f->anim, &pose, dt);
    AvatarLook look = avatar_look_from_skin(*ctx->skin);
    avatar_draw(g, meshes, &look, &pose, &f->anim, NULL);
}

/* a horizontal drag on the avatar's half spins it */
static void spin_drag(Frontend *f, MgApp *app, float x0, float x1)
{
    for (int i = 0; i < MG_MAX_TOUCHES; i++) {
        MgTouch *t = &app->input.touches[i];
        if (t->active && t->owner == MG_TOUCH_FREE && t->x0 >= x0 && t->x0 <= x1 && !t->began) {
            f->yaw += HMM_AngleDeg(0.6f) * t->dx;
            f->spin_vel = HMM_AngleDeg(0.6f) * t->dx / mg_maxf(app->frame_dt, 1e-3f) * 0.3f;
        }
    }
}

/* ------------------------------------------------------------------ helpers */

static void title_block(Ui *ui, float x, float y, float w, bool centred)
{
    const char *title = str("game_name");
    UiColor white = ui_rgba(0xFFFFFFFF), shadow = ui_rgba(0x0000006A);
    if (centred) {
        text_draw_centered(ui->gfx, ui->font, x + w * 0.5f + 2.0f, y + 26.0f, 44.0f, shadow, title);
        text_draw_centered(ui->gfx, ui->font, x + w * 0.5f, y + 24.0f, 44.0f, white, title);
        text_draw_centered(ui->gfx, ui->font, x + w * 0.5f + 0.8f, y + 24.0f, 44.0f, white, title); /* bold */
    } else {
        text_draw(ui->gfx, ui->font, x + 2.0f, y + 2.0f, 44.0f, shadow, title);
        text_draw(ui->gfx, ui->font, x, y, 44.0f, white, title);
        text_draw(ui->gfx, ui->font, x + 0.8f, y, 44.0f, white, title);
    }
}

/* greedy word wrap; returns the height used */
static float wrapped(Ui *ui, float x, float y, float w, float size, UiColor c, const char *s, bool centred)
{
    char line[160];
    float lh = text_line_height(ui->font, size);
    float yy = y;
    while (*s) {
        int n = 0, last_space = -1;
        while (s[n] && n < (int)sizeof(line) - 1) {
            line[n] = s[n];
            line[n + 1] = 0;
            if (s[n] == ' ') {
                last_space = n;
            }
            if (text_width(ui->font, line, size) > w && last_space > 0) {
                n = last_space;
                break;
            }
            n++;
        }
        line[n] = 0;
        if (centred) {
            text_draw_centered(ui->gfx, ui->font, x + w * 0.5f, yy + lh * 0.5f, size, c, line);
        } else {
            text_draw(ui->gfx, ui->font, x, yy, size, c, line);
        }
        yy += lh;
        s += n;
        while (*s == ' ') {
            s++;
        }
    }
    return yy - y;
}

static void toast_draw(Frontend *f, Ui *ui, MgApp *app)
{
    if (app->time >= f->toast_until || !f->toast[0]) {
        return;
    }
    float tw = text_width(ui->font, f->toast, 16.0f) + 40.0f;
    float x = (app->width_pt - tw) * 0.5f, y = app->height_pt - mg_maxf(app->safe_bottom, 12.0f) - 56.0f;
    ui_round_rect(ui->gfx, ui->font, x, y, tw, 40.0f, 20.0f, ui_rgba(0x101318E6));
    text_draw_centered(ui->gfx, ui->font, x + tw * 0.5f, y + 20.0f, 16.0f, ui_rgba(0xFFFFFFFF), f->toast);
}

/* a wide pill (sign-in buttons) */
static bool pill(Ui *ui, const char *id, float x, float y, float w, uint32_t bg, uint32_t fg, const char *label,
                 int mark, bool busy)
{
    bool held = false;
    bool hit = ui_hit_area(ui, id, x, y, w, 52.0f, &held);
    UiColor b = ui_rgba((bg << 8) | 0xFFu);
    if (held) {
        b.a = 200;
    }
    ui_round_rect(ui->gfx, ui->font, x, y, w, 52.0f, 26.0f, b);
    UiColor c = ui_rgba((fg << 8) | 0xFFu);
    float lx = x + w * 0.5f + 12.0f;
    if (mark == 1) { /* Google "G" */
        text_draw_centered(ui->gfx, ui->font, x + 30.0f, y + 26.0f, 24.0f, ui_rgba(0x4285F4FF), "G");
    } else if (mark == 2) { /* simple apple: body, bite, leaf */
        ui_circle(ui->gfx, ui->font, x + 27.0f, y + 29.0f, 9.0f, c);
        ui_circle(ui->gfx, ui->font, x + 33.0f, y + 29.0f, 9.0f, c);
        ui_circle(ui->gfx, ui->font, x + 39.5f, y + 26.0f, 4.0f, b);
        ui_circle(ui->gfx, ui->font, x + 32.0f, y + 15.5f, 3.0f, c);
    }
    text_draw_centered(ui->gfx, ui->font, lx, y + 26.0f, 17.0f, c, busy ? str("signing_in") : label);
    return hit && !busy;
}

/* menu row with 60 ms press feedback */
static bool menu_row(Frontend *f, Ui *ui, MgApp *app, int index, const char *label, float x, float y, float w,
                     bool highlighted, bool disabled, const char *tag)
{
    char id[24];
    SDL_snprintf(id, sizeof(id), "menurow%d", index);
    bool held = false;
    bool hit = ui_hit_area(ui, id, x, y, w, ROW_H, &held);
    if (hit || (held && f->pressed_row != index)) {
        f->pressed_row = index;
        f->pressed_at = app->time;
    }
    float s = (held || app->time - f->pressed_at < 0.06) && f->pressed_row == index ? 0.97f : 1.0f;
    float rw = w * s, rh = ROW_H * s;
    float rx = x + (w - rw) * 0.5f, ry = y + (ROW_H - rh) * 0.5f;
    ui_round_rect(ui->gfx, ui->font, rx, ry, rw, rh, 12.0f, highlighted ? k_row_hi : k_row);
    UiColor tc = disabled ? ui_rgba(0xFFFFFF66) : ui_rgba(0xFFFFFFFF);
    text_draw(ui->gfx, ui->font, rx + 18.0f, ry + rh * 0.5f - 12.0f, 20.0f, tc, label);
    if (tag) {
        float tw = text_width(ui->font, tag, 13.0f) + 16.0f;
        ui_round_rect(ui->gfx, ui->font, rx + rw - tw - 14.0f, ry + rh * 0.5f - 11.0f, tw, 22.0f, 11.0f,
                      ui_rgba(0xFFFFFF26));
        text_draw_centered(ui->gfx, ui->font, rx + rw - tw * 0.5f - 14.0f, ry + rh * 0.5f, 13.0f, ui_rgba(0xFFFFFFCC),
                           tag);
    }
    if (hit) {
        f->clicked = true;
    }
    return hit;
}

/* ------------------------------------------------------------------ screens */

static FeAction welcome(Frontend *f, Ui *ui, MgApp *app, const FrontendCtx *ctx)
{
    float w = app->width_pt, h = app->height_pt;
    float l = app->safe_left + PAD, r = w - app->safe_right - PAD;
    float top = app->safe_top + PAD + 8.0f;
    title_block(ui, l, top, r - l, true);
    wrapped(ui, l + (r - l) * 0.15f, top + 56.0f, (r - l) * 0.7f, 16.0f, ui_rgba(0xFFFFFFCC), str("description"),
            true);
    /* buttons: right half, vertically centred in the middle band */
    float cx = w * 0.5f + PAD, cw = mg_minf(r - cx, 360.0f);
    cx += (r - cx - cw) * 0.5f;
    int n = ctx->apple_available ? 2 : 1;
    float block = (float)n * 52.0f + (float)(n - 1) * 12.0f + 12.0f + 30.0f;
    float mid0 = h * 0.28f, mid1 = h * 0.80f;
    float y = mid0 + (mid1 - mid0 - block) * 0.5f;
    FeAction a = FE_NONE;
    if (ctx->google_available || !ctx->apple_available) {
        if (pill(ui, "google", cx, y, cw, 0xFFFFFF, 0x1F1F1F, str("google"), 1, ctx->signin_busy)) {
            a = FE_SIGN_IN_GOOGLE;
        }
        y += 64.0f;
    }
    if (ctx->apple_available) {
        if (pill(ui, "apple", cx, y, cw, 0x000000, 0xFFFFFF, str("apple"), 2, ctx->signin_busy)) {
            a = FE_SIGN_IN_APPLE;
        }
        y += 64.0f;
    }
    bool held;
    float gw = text_width(ui->font, str("guest"), 16.0f);
    if (ui_hit_area(ui, "guest", cx + (cw - gw) * 0.5f - 12.0f, y - 6.0f, gw + 24.0f, 36.0f, &held) &&
        !ctx->signin_busy) {
        a = FE_GUEST;
    }
    text_draw_centered(ui->gfx, ui->font, cx + cw * 0.5f, y + 12.0f, 16.0f,
                       held ? ui_rgba(0xFFFFFFFF) : ui_rgba(0xFFFFFFB3), str("guest"));
    ui_rect(ui->gfx, ui->font, cx + (cw - gw) * 0.5f, y + 24.0f, gw, 1.0f, ui_rgba(0xFFFFFF66));
    spin_drag(f, app, 0.0f, w * 0.5f);
    return a;
}

static bool name_valid(const char *s)
{
    size_t n = strlen(s);
    return n >= 2 && n <= 16 && s[0] != ' ';
}

static FeAction username(Frontend *f, Ui *ui, MgApp *app)
{
    float w = app->width_pt, h = app->height_pt;
    float x = w * 0.5f + PAD, cw = mg_minf(w - app->safe_right - PAD - x, 380.0f);
    float y = mg_maxf(app->safe_top + PAD, h * 0.22f);
    text_draw(ui->gfx, ui->font, x, y, 26.0f, ui_rgba(0xFFFFFFFF), str("username_title"));
    y += 48.0f;
    int ed = ui_text_field(ui, "username", x, y, cw, 52.0f, f->name, (int)sizeof(f->name), &f->editing_name,
                           NAME_CHARS);
    y += 60.0f;
    bool ok = name_valid(f->name);
    text_draw(ui->gfx, ui->font, x + 4.0f, y, 14.0f, ok ? ui_rgba(0xFFFFFF99) : ui_rgba(0xFF8A80FF),
              strlen(f->name) < 2 ? str("username_short") : str("username_rule"));
    y += 34.0f;
    char label[48];
    SDL_snprintf(label, sizeof(label), "%s##name_ok", str("continue"));
    bool go = ui_button(ui, label, x, y, cw, 52.0f, ok);
    spin_drag(f, app, 0.0f, w * 0.5f);
    if ((go || ed == UI_EDIT_DONE) && ok) {
        f->editing_name = false;
        return FE_NAME_DONE;
    }
    return FE_NONE;
}

static FeAction creator(Frontend *f, Ui *ui, MgApp *app, const FrontendCtx *ctx)
{
    float w = app->width_pt, h = app->height_pt;
    float x = w * 0.42f, r = w - app->safe_right - PAD, cw = r - x;
    float y = app->safe_top + PAD;
    float panel_h = h - y - mg_maxf(app->safe_bottom, PAD) - PAD;
    gfx_ui_blur_panel(ui->gfx, x - 12.0f, y - 8.0f, cw + 24.0f, panel_h + 16.0f, 18.0f, HMM_V4(0, 0, 0, 0.45f));
    ui_block(ui, x - 12.0f, y - 8.0f, cw + 24.0f, panel_h + 16.0f);
    text_draw(ui->gfx, ui->font, x, y, 24.0f, ui_rgba(0xFFFFFFFF), str("creator_title"));
    FeAction a = FE_NONE;
    char label[48];
    SDL_snprintf(label, sizeof(label), "%s##dice", str("randomise"));
    if (ui_button(ui, label, r - 110.0f, y - 4.0f, 110.0f, 36.0f, false)) {
        creator_randomise(ctx->skin);
        a = FE_SKIN_CHANGED;
    }
    y += 46.0f;
    if (creator_ui(&f->creator, ui, ctx->skin, x, y, cw, panel_h - 110.0f)) {
        a = FE_SKIN_CHANGED;
    }
    SDL_snprintf(label, sizeof(label), "%s##creator", str("done"));
    if (ui_button(ui, label, x, y + panel_h - 110.0f, cw, 52.0f, true)) {
        a = FE_CREATOR_DONE;
    }
    spin_drag(f, app, 0.0f, x - 12.0f);
    return a;
}

static FeAction main_menu(Frontend *f, Ui *ui, MgApp *app, const FrontendCtx *ctx)
{
    float w = app->width_pt, h = app->height_pt;
    float x = app->safe_left + PAD, cw = mg_minf(w * 0.5f - x - PAD, 400.0f);
    float y = app->safe_top + PAD + 8.0f;
    title_block(ui, x, y, cw, false);
    y += 54.0f;
    y += wrapped(ui, x, y, cw, 14.0f, ui_rgba(0xFFFFFFB3), str("description"), false) + 10.0f;
    ui_rect(ui->gfx, ui->font, x, y, cw, 1.0f, ui_rgba(0xFFFFFF33));
    y += 12.0f;
    FeAction a = FE_NONE;
    int row = 0;
    if (menu_row(f, ui, app, row++, str("new_game"), x, y, cw, false, false, NULL)) {
        if (ctx->has_save) {
            f->confirm_new = true;
        } else {
            a = FE_NEW_GAME;
        }
    }
    y += ROW_H + 8.0f;
    if (ctx->has_save) {
        if (menu_row(f, ui, app, row++, str("continue_game"), x, y, cw, false, false, NULL)) {
            a = FE_CONTINUE;
        }
        y += ROW_H + 8.0f;
    }
    char mp[64];
    SDL_snprintf(mp, sizeof(mp), "%s  %s", str("multiplayer"), f->mp_open ? "-" : "+");
    if (menu_row(f, ui, app, row++, mp, x, y, cw, f->mp_open, false, NULL)) {
        f->mp_open = !f->mp_open;
    }
    y += ROW_H + 8.0f;
    /* sub-list grows over 200 ms */
    float dt = mg_minf(app->frame_dt, 0.1f);
    f->mp_anim = mg_clampf(f->mp_anim + (f->mp_open ? dt : -dt) / 0.2f, 0.0f, 1.0f);
    if (f->mp_anim > 0.0f) {
        float full = (float)mode_count() * (ROW_H + 8.0f);
        float shown = full * f->mp_anim;
        gfx_ui_clip(ui->gfx, x, y, cw, shown);
        for (int i = 0; i < mode_count(); i++) {
            const ModeDef *m = mode_get(i);
            float ry = y + (float)i * (ROW_H + 8.0f);
            if (ry - y > shown) {
                break;
            }
            bool hit = menu_row(f, ui, app, 10 + i, str(m->name_key), x + 24.0f, ry, cw - 24.0f, i == ctx->last_mode,
                                !m->enabled, m->enabled ? NULL : str("soon"));
            if (hit && f->mp_anim >= 1.0f) {
                if (!m->enabled) {
                    frontend_toast(f, str("soon_toast"), app->time);
                } else {
                    f->joining = false;
                    f->code[0] = 0;
                    frontend_go(f, (FeState)m->entry_state);
                }
            }
        }
        gfx_ui_clip_off(ui->gfx);
        y += shown;
    }
    if (menu_row(f, ui, app, row++, str("settings"), x, y, cw, false, false, NULL)) {
        frontend_go(f, FE_SETTINGS);
    }
    /* footer */
    char who[96];
    if (ctx->guest) {
        SDL_snprintf(who, sizeof(who), "%s", str("guest_name"));
    } else {
        SDL_snprintf(who, sizeof(who), "%s %s " TEXT_BULLET " %s", str("signed_in_as"), ctx->name,
                     SDL_strcmp(ctx->provider, "apple") == 0 ? str("provider_apple") : str("provider_google"));
    }
    float fy = h - mg_maxf(app->safe_bottom, 10.0f) - 22.0f;
    text_draw(ui->gfx, ui->font, x, fy, 13.0f, ui_rgba(0xFFFFFF99), who);
    float vw = text_width(ui->font, ctx->version, 13.0f);
    text_draw(ui->gfx, ui->font, w - app->safe_right - PAD - vw, fy, 13.0f, ui_rgba(0xFFFFFF66), ctx->version);

    /* tapping the avatar opens the creator */
    bool held;
    if (!f->confirm_new && ui_hit_area(ui, "avatar", w * 0.62f, h * 0.15f, w * 0.26f, h * 0.7f, &held)) {
        frontend_go(f, FE_CREATOR);
    }
    spin_drag(f, app, w * 0.5f, w);

    if (f->confirm_new) { /* overwrite dialog */
        ui_block(ui, 0, 0, w, h);
        ui_rect(ui->gfx, ui->font, 0, 0, w, h, ui_rgba(0x00000080));
        float dw = 360.0f, dh = 150.0f, dx = (w - dw) * 0.5f, dy = (h - dh) * 0.5f;
        ui_panel(ui, dx, dy, dw, dh);
        text_draw_centered(ui->gfx, ui->font, dx + dw * 0.5f, dy + 40.0f, 18.0f, ui_rgba(0xFFFFFFFF), str("overwrite_q"));
        float bw = (dw - 48.0f) * 0.5f;
        char c1[48], c2[48];
        SDL_snprintf(c1, sizeof(c1), "%s##ow_cancel", str("cancel"));
        SDL_snprintf(c2, sizeof(c2), "%s##ow_new", str("new_game"));
        if (ui_button(ui, c1, dx + 16.0f, dy + dh - 64.0f, bw, 48.0f, false)) {
            f->confirm_new = false;
        }
        if (ui_button(ui, c2, dx + 32.0f + bw, dy + dh - 64.0f, bw, 48.0f, true)) {
            f->confirm_new = false;
            a = FE_NEW_GAME;
        }
    }
    return a;
}

static FeAction free_roam(Frontend *f, Ui *ui, MgApp *app)
{
    float w = app->width_pt, h = app->height_pt;
    float x = app->safe_left + PAD, cw = mg_minf(w * 0.5f - x - PAD, 420.0f);
    float y = app->safe_top + PAD + 8.0f;
    text_draw(ui->gfx, ui->font, x, y, 30.0f, ui_rgba(0xFFFFFFFF), str("mode_free_roam"));
    y += 44.0f;
    FeAction a = FE_NONE;
    char label[48];
    if (!f->joining) {
        if (menu_row(f, ui, app, 20, str("create_room"), x, y, cw, true, false, NULL)) {
            a = FE_ROOM_CREATE;
        }
        y += ROW_H + 8.0f;
        if (menu_row(f, ui, app, 21, str("join_room"), x, y, cw, false, false, NULL)) {
            f->joining = true;
            f->code[0] = 0;
        }
    } else {
        text_draw(ui->gfx, ui->font, x, y, 16.0f, ui_rgba(0xFFFFFFCC), str("room_code"));
        y += 28.0f;
        size_t len = strlen(f->code);
        for (int i = 0; i < 4; i++) {
            float bx = x + (float)i * 52.0f;
            ui_round_rect(ui->gfx, ui->font, bx, y, 44.0f, 48.0f, 10.0f,
                          (size_t)i == len ? ui_rgba(0x4A8BFF66) : ui_rgba(0xFFFFFF2A));
            char c[2] = {(size_t)i < len ? f->code[i] : 0, 0};
            text_draw_centered(ui->gfx, ui->font, bx + 22.0f, y + 24.0f, 26.0f, ui_rgba(0xFFFFFFFF), c);
        }
        y += 60.0f;
        y += ui_letter_keypad(ui, "fe", x, y, cw, mg_minf(40.0f, (h - y - 90.0f) / 3.0f - 4.0f), f->code, 4) + 8.0f;
        SDL_snprintf(label, sizeof(label), "%s##frjoin", str("join"));
        if (ui_button(ui, label, x, y, cw, 48.0f, strlen(f->code) == 4) && strlen(f->code) == 4) {
            a = FE_ROOM_JOIN;
        }
    }
    spin_drag(f, app, w * 0.5f, w);
    return a;
}

static void back_button(Frontend *f, Ui *ui, MgApp *app)
{
    char label[48];
    SDL_snprintf(label, sizeof(label), "< %s##feback", str("back"));
    if (ui_button(ui, label, app->safe_left + PAD, app->height_pt - mg_maxf(app->safe_bottom, PAD) - 48.0f, 110.0f,
                  44.0f, false)) {
        frontend_back(f);
    }
}

/* ------------------------------------------------------------------ frame */

FeAction frontend_frame(Frontend *f, Ui *ui, MgApp *app, const FrontendCtx *ctx, const AvatarMeshes *meshes)
{
    f->clicked = false;
    float side = f->state == FE_MAIN_MENU || f->state == FE_FREE_ROAM ? 0.5f : -0.5f;
    if (f->state == FE_CREATOR) {
        side = -0.55f;
    }
    draw_scene(f, app, ctx, meshes, side);
    FeAction a = FE_NONE;
    switch (f->state) {
    case FE_WELCOME:
        a = welcome(f, ui, app, ctx);
        break;
    case FE_USERNAME:
        a = username(f, ui, app);
        back_button(f, ui, app);
        break;
    case FE_CREATOR:
        a = creator(f, ui, app, ctx);
        if (f->depth > 0 && f->stack[f->depth - 1] != FE_USERNAME) {
            back_button(f, ui, app);
        }
        break;
    case FE_MAIN_MENU:
        a = main_menu(f, ui, app, ctx);
        break;
    case FE_FREE_ROAM:
        a = free_roam(f, ui, app);
        back_button(f, ui, app);
        break;
    case FE_LOADING: {
        float bw = app->width_pt * 0.4f, bx = (app->width_pt - bw) * 0.5f, by = app->height_pt * 0.82f;
        text_draw_centered(ui->gfx, ui->font, app->width_pt * 0.5f, by - 22.0f, 16.0f, ui_rgba(0xFFFFFFCC),
                           str("loading"));
        ui_round_rect(ui->gfx, ui->font, bx, by, bw, 10.0f, 5.0f, ui_rgba(0xFFFFFF33));
        ui_round_rect(ui->gfx, ui->font, bx, by, mg_maxf(bw * ctx->load_progress, 10.0f), 10.0f, 5.0f,
                      ui_rgba(0x4A8BFFFF));
        a = FE_LOAD_STEP;
        break;
    }
    default: /* SETTINGS is drawn by the game (the settings panel) */
        break;
    }
    toast_draw(f, ui, app);
    return a;
}
