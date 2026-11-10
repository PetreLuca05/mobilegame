#include "frontend.h"
#include "ui/haptics.h"

#include <SDL3/SDL_stdinc.h>
#include <string.h>

#include "gfx/camera.h"
#include "gfx/material.h"
#include "gfx/mesh.h"
#include "strings.h"
#include "ui/head_icons.h"
#include "ui/ui_widgets.h"

#define ROW_H 46.0f      /* four rows and Continue fit a 390 pt iPhone without scrolling */
#define CONTINUE_H 64.0f
#define LOBBY_PANEL_W 270.0f
#define LOBBY_ROW_H 48.0f
#define LOBBY_SLOT_FADE_S 0.25f
#define PAD 16.0f
#define EDGE 12.0f     /* the in-game HUD's margins: no safe-area insets on these screens either */
#define TOP_EDGE 8.0f
#define NAME_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _"


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
            avatar_animate(meshes, &av->anim, &pose, dt);
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
        avatar_animate(meshes, &f->anim, &hero, mg_minf(app->frame_dt, 0.1f));
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
    avatar_animate(meshes, &f->anim, &pose, dt);
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

/* A title over the 3D scene: display face, white, on an ink lip (no blurred shadow) */
static void title_block(Ui *ui, const char *title, float x, float y, float w, bool centred)
{
    const float size = 44.0f, lip = 3.0f;
    const float tx = centred ? x + (w - ui_text_w(ui, TEXT_FACE_DISPLAY, title, size)) * 0.5f : x;
    /* TILT-1: the ink copy is the slab's lip (1.5x swing); the letters sit one depth above it */
    ui_depth(ui, 1, 0, GFX_TILT_TITLE);
    gfx_ui_lip(ui->gfx, lip);
    ui_text(ui, TEXT_FACE_DISPLAY, tx, y + lip - 4.0f, size, ui_rgba(k_ui.ink), title);
    gfx_ui_lip(ui->gfx, 0.0f);
    ui_depth(ui, 2, 0, 0);
    ui_text(ui, TEXT_FACE_DISPLAY, tx, y - 4.0f, size, ui_rgba(k_ui.white), title);
    ui_depth_end(ui);
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
    /* a card from the top centre: enters on its first frame, exits in its last 0.18 s */
    const uint32_t key = ui_key("toast");
    ui_depth(ui, 3, 0, 0); /* TILT-1: toasts are a near layer */
    if (ui->anim && ui_anim_fresh(ui->anim, key)) {
        ui_haptic(HAPTIC_NOTICE); /* a toast arrives */
        ui_anim_to(ui->anim, key, UI_Y, -80.0f, 0.0f, ui->anim->reduced ? k_ui.reduced_s : k_ui.enter_s, ui->anim->reduced ? UI_EASE_LINEAR : UI_EASE_BEZIER, &k_ui.enter,
                   false);
    }
    const float left = (float)(f->toast_until - app->time);
    const float exit = left < k_ui.exit_s ? 1.0f - left / k_ui.exit_s : 0.0f;
    const float dy = (ui->anim ? ui_anim_get(ui->anim, key, UI_Y, 0.0f) : 0.0f) - 80.0f * exit * exit;
    const float tw = ui_text_w(ui, TEXT_FACE_BOLD, f->toast, 16.0f) + 44.0f;
    const float x = (app->width_pt - tw) * 0.5f, y = mg_maxf(app->safe_top, 8.0f) + 8.0f + dy;
    ui_surface(ui, x, y, tw, 44.0f, k_ui.r_card * 0.6f, k_ui.lip_card, ui_rgba(k_ui.surface), ui_rgba(k_ui.lip_mint), 0.0f, ui_rgba(0));
    ui_text_c(ui, TEXT_FACE_BOLD, x + tw * 0.5f, y + 22.0f, 16.0f, ui_rgba(k_ui.ink), f->toast);
    ui_depth_end(ui);
}

/* a wide sign-in button: the provider's mark at the left */
static bool pill(Ui *ui, const char *id, float x, float y, float w, uint32_t bg, uint32_t fg, const char *label,
                 int mark, bool busy)
{
    (void)bg;
    (void)fg;
    char key[96];
    SDL_snprintf(key, sizeof(key), "%s##%s", busy ? str("signing_in") : label, id);
    const bool hit = ui_button_v(ui, key, x, y, w, 54.0f, busy ? UI_V_DISABLED : UI_V_SECONDARY, UI_F_SMALL);
    const UiColor ink = ui_rgba(k_ui.ink);
    if (mark == 1) { /* Google "G" */
        ui_text_c(ui, TEXT_FACE_DISPLAY, x + 30.0f, y + 24.0f, 24.0f, ui_rgba(k_ui.brand_google), "G");
    } else if (mark == 2) { /* simple apple: body, bite, leaf */
        ui_circle(ui->gfx, ui->font, x + 27.0f, y + 27.0f, 9.0f, ink);
        ui_circle(ui->gfx, ui->font, x + 33.0f, y + 27.0f, 9.0f, ink);
        ui_circle(ui->gfx, ui->font, x + 39.5f, y + 24.0f, 4.0f, ui_rgba(k_ui.surface));
        ui_circle(ui->gfx, ui->font, x + 32.0f, y + 13.5f, 3.0f, ink);
    }
    return hit && !busy;
}

/* a menu row: a secondary button (or the breathing primary), an optional tag pill at the right */
static bool menu_row(Frontend *f, Ui *ui, MgApp *app, int index, const char *label, float x, float y, float w,
                     bool highlighted, bool disabled, const char *tag)
{
    (void)app;
    char id[96];
    SDL_snprintf(id, sizeof(id), "%s##menurow%d", label, index);
    ui_stagger(ui, f->stagger_n++);
    const bool hit = ui_button_v(ui, id, x, y, w, ROW_H, disabled ? UI_V_DISABLED : highlighted ? UI_V_PRIMARY : UI_V_SECONDARY,
                                 highlighted && !f->confirm_new ? UI_F_BREATHE : 0);
    if (tag) {
        const float lip = mg_minf(k_ui.lip_button, ROW_H * 0.13f), tw = ui_text_w(ui, TEXT_FACE_BOLD, tag, 13.0f) + 18.0f;
        const float ty = y + (ROW_H - lip) * 0.5f - 12.0f;
        ui_shape(ui->gfx, &(UiShape){.x = x + w - tw - 12.0f, .y = ty, .w = tw, .h = 24.0f, .radius = -1.0f, .color = ui_rgba(k_ui.surface_tint)});
        ui_text_c(ui, TEXT_FACE_BOLD, x + w - tw * 0.5f - 12.0f, ty + 12.0f, 13.0f, ui_rgba(k_ui.teal_text), tag);
    }
    ui_stagger_end(ui);
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
    return y + 12.0f;
}

static FeAction welcome(Frontend *f, Ui *ui, MgApp *app, const FrontendCtx *ctx)
{
    float w = app->width_pt;
    float cx = EDGE, cw = column_w(app);
    float y = column_top(ui, str("game_name"), app, cx, cw);
    const int buttons = (ctx->google_available || !ctx->apple_available ? 1 : 0) + (ctx->apple_available ? 1 : 0);
    const float desc_h = wrapped(ui, cx, y, cw, 15.0f, ui_rgba(0), str("description"), false); /* measured: drawn on the card below */
    ui_card(ui, cx - 10.0f, y - 12.0f, cw + 20.0f, desc_h + (float)buttons * 64.0f + 64.0f);
    y += wrapped(ui, cx, y, cw, 15.0f, ui_rgba(k_ui.ink), str("description"), false) + 16.0f;
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
    ui_text_c(ui, TEXT_FACE_BOLD, cx + cw * 0.5f, y + 12.0f, 16.0f, ui_rgba(k_ui.teal_text), str("guest"));
    ui_rect(ui->gfx, ui->font, cx + (cw - gw) * 0.5f, y + 24.0f, gw, 2.0f, ui_rgba(k_ui.teal_text));
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
    title_block(ui, str("username_title"), x, y, cw, false);
    y += 70.0f;
    ui_card(ui, x - 10.0f, y - 12.0f, cw + 20.0f, 176.0f);
    int ed = ui_text_field(ui, "username", x, y, cw, 52.0f, f->name, (int)sizeof(f->name), &f->editing_name,
                           NAME_CHARS);
    y += 60.0f;
    bool ok = name_valid(f->name);
    ui_text(ui, TEXT_FACE_BODY, x + 4.0f, y, 14.0f, ok ? ui_rgba(k_ui.ink_soft) : ui_rgba(k_ui.bad),
            strlen(f->name) < 2 ? str("username_short") : str("username_rule"));
    y += 34.0f;
    char label[48];
    SDL_snprintf(label, sizeof(label), "%s##name_ok", str("continue"));
    bool go = ui_button_v(ui, label, x, y, cw, 54.0f, ok ? UI_V_PRIMARY : UI_V_DISABLED, UI_F_BREATHE);
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
    ui_card(ui, x - 12.0f, y - 8.0f, cw + 24.0f, panel_h + 8.0f);
    ui_text(ui, TEXT_FACE_DISPLAY, x, y - 4.0f, 28.0f, ui_rgba(k_ui.ink), str("creator_title"));
    FeAction a = FE_NONE;
    char label[48];
    SDL_snprintf(label, sizeof(label), "%s##dice", str("randomise"));
    if (ui_button_v(ui, label, r - 120.0f, y - 7.0f, 120.0f, k_ui.target_min, UI_V_SECONDARY, UI_F_SMALL | UI_F_RANDOMISE)) {
        creator_randomise(ctx->skin);
        a = FE_SKIN_CHANGED;
    }
    y += 46.0f;
    if (creator_ui(&f->creator, ui, ctx->skin, x, y, cw, panel_h - 110.0f)) {
        a = FE_SKIN_CHANGED;
    }
    SDL_snprintf(label, sizeof(label), "%s##creator", str("done"));
    if (ui_button_v(ui, label, x, y + panel_h - 110.0f, cw, 54.0f, UI_V_PRIMARY, UI_F_BREATHE)) {
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
    const UiColor gold = ui_rgba(k_ui.primary);
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
    ui_card(ui, x - 10.0f, y - 8.0f, pw + 20.0f, 44.0f + 16.0f + (float)ctx->row_count * (LOBBY_ROW_H + 8.0f));
    char title[32];
    SDL_snprintf(title, sizeof(title), "%s \xC2\xB7 %d/%d", str("lobby_title"), ctx->row_count, LOBBY_MAX_PLAYERS);
    if (ctx->lobby_host) {
        ui_text(ui, TEXT_FACE_DISPLAY, x + 2.0f, y - 1.0f, 18.0f, ui_rgba(k_ui.ink), title);
        ui_text(ui, TEXT_FACE_BOLD, x + 2.0f, y + 24.0f, 12.0f, ui_rgba(k_ui.ink_soft), str(ctx->lobby_open ? "lobby_open" : "lobby_locked"));
        const float bw = 92.0f, sx = x + pw - bw - 8.0f - k_ui.toggle_w;
        bool open = ctx->lobby_open;
        if (ui_switch(ui, "lobby_switch", sx, y, &open) && !f->confirm_new) {
            *a = FE_LOBBY_TOGGLE;
        }
        char label[48];
        SDL_snprintf(label, sizeof(label), "%s##lobby_invite", str("friend_invite"));
        if (ui_button_v(ui, label, x + pw - bw, y - 2.0f, bw, 48.0f, UI_V_ACCENT, UI_F_SMALL) && !f->confirm_new) {
            *a = FE_OPEN_INVITE;
        }
    } else {
        ui_text(ui, TEXT_FACE_DISPLAY, x + 2.0f, y + 8.0f, 18.0f, ui_rgba(k_ui.ink), title);
    }
    y += 44.0f + 8.0f;
    for (int i = 0; i < ctx->row_count; i++) {
        const LobbyRow *r = &ctx->rows[i];
        char rid[24];
        SDL_snprintf(rid, sizeof(rid), "lobby_row%d", i);
        ui_row(ui, rid, x, y, pw, LOBBY_ROW_H, false);
        head_icon_draw(ctx->heads, ui->gfx, ui->font, &r->skin, r->name, x + 26.0f, y + LOBBY_ROW_H * 0.5f, 40.0f, false);
        const float tx = x + 52.0f;
        float room = pw - 52.0f - 8.0f;
        char id[24];
        if (ctx->lobby_host && !r->me) { /* kick */
            const float cx = x + pw - 24.0f, cy = y + LOBBY_ROW_H * 0.5f;
            SDL_snprintf(id, sizeof(id), "lobby_kick%d", i);
            if (ui_icon_button(ui, id, UI_ICON_CLOSE, cx - 22.0f, cy - 24.0f, 44.0f, UI_V_SECONDARY) && !f->confirm_new) {
                SDL_snprintf(f->kick_id, sizeof(f->kick_id), "%s", r->id);
                *a = FE_LOBBY_KICK;
            }
            room -= 44.0f;
        } else if (!ctx->lobby_host && r->me) { /* leave */
            char label[48];
            SDL_snprintf(label, sizeof(label), "%s##lobby_leave", str("lobby_leave"));
            if (ui_button_v(ui, label, x + pw - 76.0f, y + 1.0f, 70.0f, k_ui.target_min, UI_V_SECONDARY, UI_F_SMALL) && !f->confirm_new) {
                *a = FE_LOBBY_LEAVE;
            }
            room -= 72.0f;
        }
        room -= r->host ? 22.0f : 0.0f; /* the crown */
        float size = 16.0f;
        const float tw = text_width(ui->font, r->name, size);
        size = tw > room ? size * room / tw : size;
        ui_text(ui, TEXT_FACE_BOLD, tx, y + LOBBY_ROW_H * 0.5f - size * 0.75f, size, ui_rgba(k_ui.ink), r->name);
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
    f->stagger_n = 0;
    float w = app->width_pt, h = app->height_pt;
    float x = EDGE, cw = column_w(app);
    float y = TOP_EDGE + 8.0f;
    title_block(ui, str("menu_title"), x, y - 8.0f, cw, false);
    y += 54.0f;
    y += 12.0f;
    FeAction a = FE_NONE;
    /* the account, top right: opens Friends */
    const float chip_r = w - EDGE, chip_w = head_chip_width(ui->font, ctx->lobby_visible ? ctx->name : str("lobby_offline_chip")), chip_y = TOP_EDGE;
    bool chip_held = false;
    ui->hclass = HC_PILL;
    if (ui_hit_area(ui, "account_chip", chip_r - chip_w, chip_y, chip_w, 44.0f, &chip_held) && !f->confirm_new) {
        a = FE_OPEN_FRIENDS;
    }
    const char *chip_name = ctx->lobby_visible ? ctx->name : str("lobby_offline_chip");
    ui_depth(ui, 2, 0, 0); /* TILT-1: the account pill */
    head_chip_draw(ctx->heads, ui->gfx, ui->font, ctx->skin, chip_name, chip_r, chip_y);
    ui_depth_end(ui);
    if (chip_held) {
        ui_round_rect(ui->gfx, ui->font, chip_r - chip_w, chip_y, chip_w, 44.0f, 22.0f, ui_rgba(k_ui.glass));
    }
    if (ctx->lobby_visible) {
        lobby_panel(f, ui, app, ctx, chip_y + 44.0f + 8.0f, &a);
    }
    if (ctx->friend_requests > 0) {
        char n[8];
        SDL_snprintf(n, sizeof(n), "%d", ctx->friend_requests);
        ui_circle(ui->gfx, ui->font, chip_r - 4.0f, chip_y + 4.0f, 11.0f, ui_rgba(k_ui.bad));
        ui_text_c(ui, TEXT_FACE_DISPLAY, chip_r - 4.0f, chip_y + 4.0f, 12.0f, ui_rgba(k_ui.white), n);
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
    } else if (menu_row(f, ui, app, 0, str("new_game"), x, y, cw, !show_continue, false, NULL)) { /* the main button when there is no save */
        if (ctx->has_save) {
            f->confirm_new = true;
        } else {
            a = FE_NEW_GAME;
        }
    }
    y += ROW_H + 8.0f;
    if (menu_row(f, ui, app, 4, str("maps"), x, y, cw, false, guest, ctx->maps_tag) && !guest) {
        a = FE_OPEN_MAPS;
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
        if (ui_button_v(ui, label, x, bottom - CONTINUE_H, cw, CONTINUE_H, UI_V_PRIMARY, f->confirm_new ? 0 : UI_F_BREATHE)) {
            a = FE_CONTINUE;
        }
    }
    float vw = text_width(ui->font, ctx->version, 13.0f);
    ui_text(ui, TEXT_FACE_BOLD, w - EDGE - vw, fy, 13.0f, ui_rgba(k_ui.white_soft), ctx->version);

    if (f->confirm_new) { /* overwrite dialog */
        float dw = 360.0f, dh = 160.0f, dx, dy;
        ui_modal_warning(ui, "overwrite", dw, dh, &dx, &dy);
        ui_text_c(ui, TEXT_FACE_DISPLAY, dx + dw * 0.5f, dy + 40.0f, 22.0f, ui_rgba(k_ui.ink), str("overwrite_q"));
        float bw = (dw - 48.0f) * 0.5f;
        char c1[48], c2[48];
        SDL_snprintf(c1, sizeof(c1), "%s##ow_cancel", str("cancel"));
        SDL_snprintf(c2, sizeof(c2), "%s##ow_new", str("new_game"));
        if (ui_button_v(ui, c1, dx + 16.0f, dy + dh - 66.0f, bw, 50.0f, UI_V_SECONDARY, UI_F_SMALL)) {
            f->confirm_new = false;
        }
        if (ui_button_v(ui, c2, dx + 32.0f + bw, dy + dh - 66.0f, bw, 50.0f, UI_V_DESTRUCTIVE, UI_F_SMALL | UI_F_CONFIRM)) { /* it overwrites */
            f->confirm_new = false;
            a = FE_NEW_GAME;
        }
        ui_modal_end(ui);
    }
    return a;
}

static void back_button(Frontend *f, Ui *ui, MgApp *app, float bx)
{
    char label[48];
    SDL_snprintf(label, sizeof(label), "%s##feback", str("back"));
    const float by = app->height_pt - EDGE - 54.0f;
    if (ui_button_v(ui, label, bx, by, 120.0f, 54.0f, UI_V_SECONDARY, UI_F_SMALL)) {
        frontend_back(f);
    }
    ui_icon(ui, UI_ICON_BACK, bx + 22.0f, by + 23.0f, 22.0f, ui_rgba(k_ui.ink));
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
        ui_text_c(ui, TEXT_FACE_DISPLAY, app->width_pt * 0.5f, by - 24.0f, 18.0f, ui_rgba(k_ui.white), str("loading"));
        ui_progress(ui, "loading", bx, by, bw, k_ui.progress_h, ctx->load_progress);
        a = FE_LOAD_STEP;
        break;
    }
    default: /* SETTINGS is drawn by the game (the settings panel) */
        break;
    }
    frontend_toast_draw(f, ui, app);
    return a;
}
