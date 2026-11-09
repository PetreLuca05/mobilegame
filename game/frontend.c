#include "frontend.h"

#include <SDL3/SDL_stdinc.h>
#include <string.h>

#include "gfx/camera.h"
#include "gfx/material.h"
#include "gfx/mesh.h"
#include "strings.h"
#include "ui/head_icons.h"

#define ROW_H 52.0f
#define CONTINUE_H 72.0f
#define LOBBY_PANEL_W 270.0f
#define LOBBY_ROW_H 48.0f
#define LOBBY_SLOT_FADE_S 0.25f
#define PAD 16.0f
#define EDGE 12.0f     /* the in-game HUD's margins: no safe-area insets on these screens either */
#define TOP_EDGE 8.0f
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
    if (ctx->world_behind && f->state == FE_MAIN_MENU && ctx->row_count > 0) { /* the whole lobby, in a line */
        const float dt = mg_minf(app->frame_dt, 0.1f);
        for (int e = 0; e < (int)MG_ARRAY_COUNT(f->avatars); e++) {
            f->avatars[e].listed = false;
        }
        for (int i = 0; i < ctx->row_count; i++) {
            const LobbyRow *r = &ctx->rows[i];
            int found = -1, free_at = -1;
            for (int e = 0; e < (int)MG_ARRAY_COUNT(f->avatars); e++) {
                if (f->avatars[e].active && strcmp(f->avatars[e].id, r->id) == 0) {
                    found = e;
                } else if (!f->avatars[e].active && free_at < 0) {
                    free_at = e;
                }
            }
            if (found < 0 && free_at >= 0) {
                found = free_at;
                memset(&f->avatars[found], 0, sizeof(f->avatars[found]));
                f->avatars[found].active = true;
                SDL_snprintf(f->avatars[found].id, sizeof(f->avatars[found].id), "%s", r->id);
            }
            if (found >= 0) {
                FeAvatar *av = &f->avatars[found];
                av->listed = true;
                av->at = i;
                av->skin = r->skin;
                av->host = r->host;
                SDL_snprintf(av->name, sizeof(av->name), "%s", r->name);
            }
        }
        for (int e = 0; e < (int)MG_ARRAY_COUNT(f->avatars); e++) {
            FeAvatar *av = &f->avatars[e];
            if (!av->active) {
                continue;
            }
            av->fade = av->listed ? mg_minf(av->fade + dt / LOBBY_SLOT_FADE_S, 1.0f) : mg_maxf(av->fade - dt / LOBBY_SLOT_FADE_S, 0.0f);
            if (av->fade <= 0.0f && !av->listed) {
                av->active = false;
                continue;
            }
            /* the host keeps the hero's turn; the others look slightly inward */
            const float yaw = av->at == 0 ? HMM_PI32 - HMM_AngleDeg(45.0f) : HMM_PI32 + HMM_AngleDeg(14.0f);
            AvatarPose pose = {ctx->slots[av->at], yaw, HMM_V3(0, 0, 0), true, false, false, false};
            pose.flat_ground = true;
            pose.hide = 1.0f - av->fade;
            avatar_animate(&av->anim, &pose, dt);
            AvatarLook look = avatar_look_from_skin(av->skin);
            avatar_draw(g, meshes, &look, &pose, &av->anim, NULL);
        }
        return;
    }
    if (ctx->world_behind) { /* the hospital is drawn already: the hero stands in front, turned 45 degrees to its right */
        const float turn_dt = mg_minf(app->frame_dt, 0.1f);
        f->hero_turn = f->state == FE_CREATOR ? f->hero_turn + f->spin_vel * turn_dt : 0.0f;
        f->spin_vel *= 1.0f - mg_minf(turn_dt * 3.0f, 1.0f);
        AvatarPose hero = {ctx->hero_feet, HMM_PI32 - HMM_AngleDeg(45.0f) + f->hero_turn, HMM_V3(0, 0, 0), true, false, false, false};
        hero.flat_ground = true; /* the contact disc */
        avatar_animate(&f->anim, &hero, mg_minf(app->frame_dt, 0.1f));
        AvatarLook hero_look = avatar_look_from_skin(*ctx->skin);
        avatar_draw(g, meshes, &hero_look, &hero, &f->anim, NULL);
        return;
    }
    float aspect = app->width_pt / app->height_pt;
    const float fov = HMM_AngleDeg(40.0f), dist = 5.2f;
    float tan_x = HMM_TanF(fov * 0.5f) * aspect;
    float tx = side * tan_x * dist; /* +X shows on the left from this side */
    GfxSceneParams sp = {0};
    sp.eye = HMM_V3(tx, 1.75f, -dist);
    sp.view = HMM_LookAt_RH(sp.eye, HMM_V3(tx, 1.3f, 0.0f), HMM_V3(0, 1, 0));
    sp.proj = camera_projection(fov, aspect, 0.05f, 40.0f, 0.0f);
#ifdef MG_RENDER_FULL
    sp.sun_dir = HMM_NormV3(HMM_V3(-0.3f, 0.8f, -0.6f)); /* soft fill from the front */
    sp.sun_color = HMM_V3(0.16f, 0.16f, 0.18f);
#else /* no spotlight: the key light is the overhead lamp */
    sp.sun_dir = HMM_NormV3(HMM_V3(0.0f, 1.0f, 0.42f));
    sp.sun_color = HMM_V3(0.62f, 0.6f, 0.56f);
#endif
    sp.ambient = HMM_V3(0.42f, 0.44f, 0.5f);
    sp.sky_horizon = mg_rgb(0x2A2F3A);
    sp.sky_zenith = mg_rgb(0x1E222B);
    sp.time = (float)app->time;
    gfx_set_scene(g, &sp);

#ifdef MG_RENDER_FULL
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
#endif
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

static void title_block(Ui *ui, const char *title, float x, float y, float w, bool centred)
{
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

bool frontend_toast_active(const Frontend *f, double now) { return now < f->toast_until && f->toast[0]; }

void frontend_toast_draw(Frontend *f, Ui *ui, MgApp *app)
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

/* The menu's left column, shared by the first-launch screens: the hero stands to its right. */
static float column_w(const MgApp *app)
{
    return mg_minf(app->width_pt * 0.5f - EDGE - PAD, 270.0f);
}

static float creator_w(const MgApp *app) { return app->width_pt * 0.58f - EDGE - 12.0f; }

/* Title and divider; returns where the content starts. */
static float column_top(Ui *ui, const char *title, const MgApp *app, float x, float w)
{
    float y = TOP_EDGE + 8.0f;
    title_block(ui, title, x, y - 8.0f, w, false);
    y += 54.0f;
    ui_rect(ui->gfx, ui->font, x, y, w, 1.0f, ui_rgba(0xFFFFFF33));
    return y + 12.0f;
}

static FeAction welcome(Frontend *f, Ui *ui, MgApp *app, const FrontendCtx *ctx)
{
    float w = app->width_pt;
    float cx = EDGE, cw = column_w(app);
    float y = column_top(ui, str("game_name"), app, cx, cw);
    y += wrapped(ui, cx, y, cw, 15.0f, ui_rgba(0xFFFFFFFF), str("description"), false) + 16.0f;
    const int buttons = (ctx->google_available || !ctx->apple_available ? 1 : 0) + (ctx->apple_available ? 1 : 0);
    gfx_ui_blur_panel(ui->gfx, cx - 8.0f, y - 12.0f, cw + 16.0f, (float)buttons * 64.0f + 54.0f, 18.0f, HMM_V4(0, 0, 0, 0.45f));
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
    float w = app->width_pt;
    float x = EDGE, cw = column_w(app);
    float y = TOP_EDGE + 8.0f;
    text_draw(ui->gfx, ui->font, x, y + 6.0f, 22.0f, ui_rgba(0xFFFFFFFF), str("username_title"));
    y += 54.0f;
    ui_rect(ui->gfx, ui->font, x, y, cw, 1.0f, ui_rgba(0xFFFFFF33));
    y += 16.0f;
    gfx_ui_blur_panel(ui->gfx, x - 8.0f, y - 12.0f, cw + 16.0f, 170.0f, 18.0f, HMM_V4(0, 0, 0, 0.45f));
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
    float x = EDGE + 12.0f, cw = creator_w(app), r = x + cw; /* the frosted panel starts at the edge margin */
    float y = TOP_EDGE + 8.0f;
    float panel_h = h - y - EDGE - 8.0f;
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
    spin_drag(f, app, r + 12.0f, w);
    return a;
}

float frontend_menu_right(const MgApp *app, FeState state)
{
    return state == FE_CREATOR ? EDGE + 12.0f + creator_w(app) + 12.0f : EDGE + column_w(app);
}

float frontend_menu_free_right(const MgApp *app, bool lobby_panel)
{
    return lobby_panel ? app->width_pt - EDGE - LOBBY_PANEL_W - 8.0f : app->width_pt;
}

void frontend_draw_crown(GfxContext *g, const TextFont *font, float x, float y, float size)
{
    const UiColor gold = ui_rgba(0xFFC940FF);
    const float s = size / 14.0f, t = 2.0f * s;
    const float px[7] = {0.0f, 0.0f, 4.0f, 7.0f, 10.0f, 14.0f, 14.0f}, py[7] = {11.0f, 3.0f, 6.5f, 0.5f, 6.5f, 3.0f, 11.0f};
    for (int i = 0; i < 6; i++) {
        ui_line(g, font, x + px[i] * s, y + py[i] * s, x + px[i + 1] * s, y + py[i + 1] * s, t, gold);
    }
    ui_line(g, font, x, y + 11.0f * s, x + 14.0f * s, y + 11.0f * s, t, gold);
}

/* Right side, under the account chip: header, then one row per player. */
static void lobby_panel(Frontend *f, Ui *ui, MgApp *app, const FrontendCtx *ctx, float top, FeAction *a)
{
    const float pw = LOBBY_PANEL_W, x = app->width_pt - EDGE - pw;
    float y = top;
    char title[32];
    SDL_snprintf(title, sizeof(title), "%s \xC2\xB7 %d/%d", str("lobby_title"), ctx->row_count, LOBBY_MAX_PLAYERS);
    if (ctx->lobby_host) {
        text_draw(ui->gfx, ui->font, x + 4.0f, y + 3.0f, 16.0f, ui_rgba(0xFFFFFFFF), title);
        text_draw(ui->gfx, ui->font, x + 4.0f, y + 24.0f, 12.0f, ui_rgba(0xFFFFFFB3), str(ctx->lobby_open ? "lobby_open" : "lobby_locked"));
        const float bw = 92.0f, sw = 46.0f, sh = 26.0f, sx = x + pw - bw - 10.0f - sw, sy = y + 9.0f;
        bool held;
        if (ui_hit_area(ui, "lobby_switch", sx - 6.0f, y, sw + 12.0f, 44.0f, &held) && !f->confirm_new) {
            *a = FE_LOBBY_TOGGLE;
        }
        ui_round_rect(ui->gfx, ui->font, sx, sy, sw, sh, sh * 0.5f, ctx->lobby_open ? ui_rgba(0x34C759FF) : ui_rgba(0xFFFFFF40));
        ui_circle(ui->gfx, ui->font, ctx->lobby_open ? sx + sw - sh * 0.5f : sx + sh * 0.5f, sy + sh * 0.5f, sh * 0.5f - 3.0f, ui_rgba(0xFFFFFFFF));
        char label[48];
        SDL_snprintf(label, sizeof(label), "%s##lobby_invite", str("friend_invite"));
        if (ui_button(ui, label, x + pw - bw, y, bw, 44.0f, true) && !f->confirm_new) {
            *a = FE_OPEN_INVITE;
        }
    } else {
        text_draw(ui->gfx, ui->font, x + 4.0f, y + 12.0f, 16.0f, ui_rgba(0xFFFFFFFF), title);
    }
    y += 44.0f + 8.0f;
    for (int i = 0; i < ctx->row_count; i++) {
        const LobbyRow *r = &ctx->rows[i];
        ui_round_rect(ui->gfx, ui->font, x, y, pw, LOBBY_ROW_H, 12.0f, k_row);
        head_icon_draw(ctx->heads, ui->gfx, ui->font, &r->skin, r->name, x + 26.0f, y + LOBBY_ROW_H * 0.5f, 40.0f, false);
        const float tx = x + 52.0f;
        float room = pw - 52.0f - 8.0f;
        char id[24];
        if (ctx->lobby_host && !r->me) { /* kick */
            const float cx = x + pw - 24.0f, cy = y + LOBBY_ROW_H * 0.5f;
            SDL_snprintf(id, sizeof(id), "lobby_kick%d", i);
            bool held;
            if (ui_hit_area(ui, id, cx - 22.0f, cy - 22.0f, 44.0f, 44.0f, &held) && !f->confirm_new) {
                SDL_snprintf(f->kick_id, sizeof(f->kick_id), "%s", r->id);
                *a = FE_LOBBY_KICK;
            }
            ui_circle(ui->gfx, ui->font, cx, cy, 16.0f, held ? ui_rgba(0xFFFFFF55) : ui_rgba(0xFFFFFF26));
            ui_line(ui->gfx, ui->font, cx - 5.0f, cy - 5.0f, cx + 5.0f, cy + 5.0f, 2.5f, ui_rgba(0xFFFFFFFF));
            ui_line(ui->gfx, ui->font, cx + 5.0f, cy - 5.0f, cx - 5.0f, cy + 5.0f, 2.5f, ui_rgba(0xFFFFFFFF));
            room -= 44.0f;
        } else if (!ctx->lobby_host && r->me) { /* leave */
            char label[48];
            SDL_snprintf(label, sizeof(label), "%s##lobby_leave", str("lobby_leave"));
            if (ui_button(ui, label, x + pw - 72.0f, y + 5.0f, 66.0f, 38.0f, false) && !f->confirm_new) {
                *a = FE_LOBBY_LEAVE;
            }
            room -= 72.0f;
        }
        room -= r->host ? 22.0f : 0.0f; /* the crown */
        float size = 16.0f;
        const float tw = text_width(ui->font, r->name, size);
        size = tw > room ? size * room / tw : size;
        text_draw(ui->gfx, ui->font, tx, y + LOBBY_ROW_H * 0.5f - size * 0.65f, size, ui_rgba(0xFFFFFFFF), r->name);
        if (r->host) {
            frontend_draw_crown(ui->gfx, ui->font, tx + text_width(ui->font, r->name, size) + 6.0f, y + LOBBY_ROW_H * 0.5f - 7.0f, 14.0f);
        }
        y += LOBBY_ROW_H + 8.0f;
    }
    f->lobby_x = x;
    f->lobby_y = top;
    f->lobby_w = pw;
    f->lobby_h = y - top - 8.0f;
}

static FeAction main_menu(Frontend *f, Ui *ui, MgApp *app, const FrontendCtx *ctx)
{
    float w = app->width_pt, h = app->height_pt;
    float x = EDGE, cw = column_w(app);
    float y = TOP_EDGE + 8.0f;
    title_block(ui, str("menu_title"), x, y - 8.0f, cw, false);
    y += 54.0f;
    ui_rect(ui->gfx, ui->font, x, y, cw, 1.0f, ui_rgba(0xFFFFFF33));
    y += 12.0f;
    FeAction a = FE_NONE;
    /* the account, top right: opens Friends */
    const float chip_r = w - EDGE, chip_w = head_chip_width(ui->font, ctx->lobby_visible ? ctx->name : str("lobby_offline_chip")), chip_y = TOP_EDGE;
    bool chip_held = false;
    if (ui_hit_area(ui, "account_chip", chip_r - chip_w, chip_y, chip_w, 44.0f, &chip_held) && !f->confirm_new) {
        a = FE_OPEN_FRIENDS;
    }
    const char *chip_name = ctx->lobby_visible ? ctx->name : str("lobby_offline_chip");
    head_chip_draw(ctx->heads, ui->gfx, ui->font, ctx->skin, chip_name, chip_r, chip_y);
    if (chip_held) {
        ui_round_rect(ui->gfx, ui->font, chip_r - chip_w, chip_y, chip_w, 44.0f, 22.0f, ui_rgba(0xFFFFFF26));
    }
    if (ctx->lobby_visible) {
        lobby_panel(f, ui, app, ctx, chip_y + 44.0f + 8.0f, &a);
    }
    if (ctx->friend_requests > 0) {
        char n[8];
        SDL_snprintf(n, sizeof(n), "%d", ctx->friend_requests);
        ui_circle(ui->gfx, ui->font, chip_r - 4.0f, chip_y + 4.0f, 11.0f, ui_rgba(0xE5484DFF));
        text_draw_centered(ui->gfx, ui->font, chip_r - 4.0f, chip_y + 4.0f, 12.0f, ui_rgba(0xFFFFFFFF), n);
    }
    /* the rows scroll between the divider and the account line when the screen is short */
    const float fy = h - EDGE - 16.0f;
    const float bottom = h - EDGE; /* Continue sits here when there is a save */
    const bool guest = ctx->lobby_visible && !ctx->lobby_host; /* only the host starts the game */
    const bool show_continue = ctx->has_save && !guest;
    const float rows_top = y, rows_view = mg_maxf(bottom - y - (show_continue ? CONTINUE_H + 12.0f : 0.0f), ROW_H);
    const float rows_off = ui_scroll_begin(ui, &f->menu_scroll, x - 4.0f, rows_top, cw + 8.0f, rows_view, false);
    y -= rows_off;
    if (guest) {
        menu_row(f, ui, app, 0, str("lobby_wait_host"), x, y, cw, false, true, NULL);
    } else if (menu_row(f, ui, app, 0, str("new_game"), x, y, cw, false, false, NULL)) {
        if (ctx->has_save) {
            f->confirm_new = true;
        } else {
            a = FE_NEW_GAME;
        }
    }
    y += ROW_H + 8.0f;
    if (menu_row(f, ui, app, 31, str("locker"), x, y, cw, false, false, NULL)) {
        frontend_go(f, FE_CREATOR);
    }
    y += ROW_H + 8.0f;
    if (menu_row(f, ui, app, 2, str("settings"), x, y, cw, false, false, NULL)) {
        frontend_go(f, FE_SETTINGS);
    }
    y += ROW_H;
    ui_scroll_end(ui, &f->menu_scroll, rows_view, y + rows_off - rows_top);
    if (show_continue) {
        char label[48];
        SDL_snprintf(label, sizeof(label), "%s##menurow1", str("continue_game"));
        if (ui_button(ui, label, x, bottom - CONTINUE_H, cw, CONTINUE_H, true)) {
            a = FE_CONTINUE;
        }
    }
    float vw = text_width(ui->font, ctx->version, 13.0f);
    text_draw(ui->gfx, ui->font, w - EDGE - vw, fy, 13.0f, ui_rgba(0xFFFFFF66), ctx->version);

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

static void back_button(Frontend *f, Ui *ui, MgApp *app, float bx)
{
    char label[48];
    SDL_snprintf(label, sizeof(label), "   %s##feback", str("back"));
    const float by = app->height_pt - EDGE - 48.0f;
    if (ui_button(ui, label, bx, by, 110.0f, 44.0f, false)) {
        frontend_back(f);
    }
    /* procedural arrow: the font has no left arrow */
    ui_line(ui->gfx, ui->font, bx + 26.0f, by + 14.0f, bx + 18.0f, by + 22.0f, 3.0f, ui_rgba(0xFFFFFFFF));
    ui_line(ui->gfx, ui->font, bx + 18.0f, by + 22.0f, bx + 26.0f, by + 30.0f, 3.0f, ui_rgba(0xFFFFFFFF));
}

/* ------------------------------------------------------------------ frame */

FeAction frontend_frame(Frontend *f, Ui *ui, MgApp *app, const FrontendCtx *ctx, const AvatarMeshes *meshes)
{
    f->clicked = false;
    draw_scene(f, app, ctx, meshes, 0.5f); /* the controls are on the left */
    FeAction a = FE_NONE;
    switch (f->state) {
    case FE_WELCOME:
        a = welcome(f, ui, app, ctx);
        break;
    case FE_USERNAME:
        a = username(f, ui, app);
        back_button(f, ui, app, EDGE);
        break;
    case FE_CREATOR:
        a = creator(f, ui, app, ctx);
        if (f->depth > 0 && f->stack[f->depth - 1] != FE_USERNAME) {
            back_button(f, ui, app, frontend_menu_right(app, FE_CREATOR) + 12.0f);
        }
        break;
    case FE_MAIN_MENU:
        a = main_menu(f, ui, app, ctx);
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
    frontend_toast_draw(f, ui, app);
    return a;
}
