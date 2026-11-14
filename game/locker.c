#include "locker.h"

#include <SDL3/SDL_stdinc.h>
#include <math.h>

#include "creator.h"
#include "gfx/material.h"
#include "gfx/mesh.h"
#include "strings.h"
#include "ui/haptics.h"
#include "ui/layout.h"
#include "ui/ui_fx.h"
#include "ui/ui_style.h"
#include "ui/ui_widgets.h"

#define SWING_DEG 25.0f     /* the idle ping-pong: +-25 degrees */
#define SWING_PERIOD_S 4.0f /* one left-right-left */
#define SWING_RETURN_S 1.2f /* after a drag, the swing's weight comes back over this */
#define TURN_DEG_PER_PT 0.6f /* the hero's drag, as on the other screens */
#define STAGE_FROM 0.55f    /* the backdrop fades in over the last part of the camera move (creator_t 0.55 .. 1) */
#define ENTER_S 0.45f       /* the backdrop card's enter */
#define TAP_SLOP_PT 10.0f   /* a wheel touch that moved less than this is a tap on a slot */
#define SNAP_RATE 14.0f     /* 1 / s: the wheel settling on its middle item */
#define FRICTION 4.5f       /* 1 / s: the wheel's inertia */
#define WHEEL_REACH 1.6f    /* slots from the middle that are drawn: fading from 1 to here, then gone (inside the wheel's rect) */

static const char *const k_tab_keys[LOCKER_TABS] = {"tab_body", "tab_skin", "hair_style", "tab_hair_colour", "tab_suit", "tab_extras"};
static const int k_counts[LOCKER_TABS] = {2, AVATAR_SKIN_COLORS, AVATAR_HAIRS, AVATAR_HAIR_COLORS, AVATAR_SUITS, AVATAR_ACCESSORIES};
/* the wheel shows heads for what is on the head, whole avatars for the rest */
static const bool k_head_only[LOCKER_TABS] = {false, true, true, true, false, false};

static uint8_t *field(NetSkin *s, int tab)
{
    switch (tab) {
    case 0: return &s->body;
    case 1: return &s->skin;
    case 2: return &s->hair;
    case 3: return &s->hair_color;
    case 4: return &s->suit;
    default: return &s->accessory;
    }
}

static int field_c(const NetSkin *s, int tab) { return *field((NetSkin *)s, tab); }

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

bool locker_init(Locker *k, GfxContext *g, Arena *scratch)
{
    SDL_zerop(k);
    k->touch = -1;
    k->shown_sel = -1;
    const size_t mark = arena_mark(scratch);
    MeshBuilder mb;
    const HMM_Vec3 n = HMM_V3(0, 0, 1);
    const HMM_Vec4 white = HMM_V4(1, 1, 1, 1);
    bool ok = mb_init(&mb, scratch, 8, 8);
    if (ok) { /* a unit quad: x, y in 0..1 */
        const uint32_t a = mb_vertex(&mb, HMM_V3(0, 0, 0), n, white), b = mb_vertex(&mb, HMM_V3(1, 0, 0), n, white);
        const uint32_t c = mb_vertex(&mb, HMM_V3(1, 1, 0), n, white), d = mb_vertex(&mb, HMM_V3(0, 1, 0), n, white);
        mb_triangle_idx(&mb, a, c, b); /* wound for the stage's y-down projection (a mirror): front-facing there */
        mb_triangle_idx(&mb, a, d, c);
        k->quad = mb_upload(&mb, g);
    }
    arena_reset_to(scratch, mark);
    enum { SEG = 32 };
    ok = ok && mb_init(&mb, scratch, SEG + 2, SEG * 3);
    if (ok) { /* a unit disc (radius 1) */
        const uint32_t c = mb_vertex(&mb, HMM_V3(0, 0, 0), n, white);
        uint32_t first = 0, prev = 0;
        for (int i = 0; i <= SEG; i++) {
            const float a = (float)i / (float)SEG * 2.0f * HMM_PI32;
            const uint32_t v = i == SEG ? first : mb_vertex(&mb, HMM_V3(cosf(a), sinf(a), 0), n, white);
            if (i == 0) {
                first = v;
            } else {
                mb_triangle_idx(&mb, c, v, prev); /* as the quad: front-facing under the y-down stage */
            }
            prev = v;
        }
        k->disc = mb_upload(&mb, g);
    }
    arena_reset_to(scratch, mark);
    return ok && k->quad != GFX_MESH_NONE && k->disc != GFX_MESH_NONE;
}

void locker_layout(const MgApp *app, LockerLayout *l)
{
    const ui_frame_t f = ui_hud_frame(app->width_pt, app->height_pt, app->safe_left, app->safe_right, app->safe_top, app->safe_bottom, 0.0f);
    const float w = app->width_pt, h = app->height_pt;
    const float left = f.inset_l + 12.0f, right = w - f.inset_r - 12.0f;
    l->top = f.inset_t + 8.0f;
    l->bottom = h - f.inset_b - 12.0f;
    l->tabs_x = left;
    l->tabs_w = clampf(w * 0.2f, 150.0f, 230.0f);
    l->wheel_w = clampf(w * 0.17f, 128.0f, 190.0f);
    l->wheel_x = right - l->wheel_w;
    l->stage_x = l->tabs_x + l->tabs_w + 12.0f;
    l->stage_w = l->wheel_x - 12.0f - l->stage_x;
    l->card_x = l->stage_x, l->card_y = l->top, l->card_w = l->stage_w, l->card_h = l->bottom - l->top;
    const float buttons_h = k_ui.target_min + 8.0f;
    l->buttons_y = l->bottom - k_ui.target_min;
    l->wheel_y = l->top;
    l->wheel_h = l->buttons_y - 12.0f - l->top;
    l->slot = clampf(l->wheel_h / 4.2f, k_ui.target_min + 8.0f, 150.0f); /* the ends' slots half under the wheel's edges, like a drum */
    (void)buttons_h;
}

float locker_centre_x(const MgApp *app)
{
    LockerLayout l;
    locker_layout(app, &l);
    return l.stage_x + l.stage_w * 0.5f;
}

/* the middle slot's item and how far each slot sits from the middle (in slots) */
static int sel_of(const Locker *k, int tab) { return (int)clampf(floorf(k->pos[tab] + 0.5f), 0.0f, (float)(k_counts[tab] - 1)); }

static void wheel_input(Locker *k, Ui *ui, MgApp *app, const LockerLayout *l, NetSkin *skin, bool *changed)
{
    const int tab = k->tab, n = k_counts[tab];
    const float dt = mg_minf(app->frame_dt, 0.1f), cy = l->wheel_y + l->wheel_h * 0.5f;
    bool held = false;
    ui->hclass = HC_SWATCH; /* a confirmed tap: the creator-option haptic; the detents below have their own row */
    const bool clicked = ui_press(ui, "crwheel", l->wheel_x, l->wheel_y, l->wheel_w, l->wheel_h, &held, true);
    if (ui->active_id == ui_key("crwheel") && ui->active_touch >= 0) {
        const MgTouch *t = &ui->in->touches[ui->active_touch];
        if (!k->drag) {
            k->drag = true;
            k->touch = ui->active_touch;
            k->drag_y0 = t->y0, k->drag_pos0 = k->pos[tab], k->drag_move = 0.0f;
            k->vel = 0.0f;
        }
        k->drag_move = mg_maxf(k->drag_move, fabsf(t->y - k->drag_y0));
        if (k->drag_move >= TAP_SLOP_PT) {
            const float before = k->pos[tab];
            k->pos[tab] = k->drag_pos0 - (t->y - k->drag_y0) / l->slot;
            /* rubber band past the ends */
            if (k->pos[tab] < 0.0f) {
                k->pos[tab] *= 0.35f;
            } else if (k->pos[tab] > (float)(n - 1)) {
                k->pos[tab] = (float)(n - 1) + (k->pos[tab] - (float)(n - 1)) * 0.35f;
            }
            const float rate = (k->pos[tab] - before) / mg_maxf(dt, 1e-3f);
            k->last_dy_rate = k->last_dy_rate * 0.6f + rate * 0.4f;
            k->drag_frames += 1.0f;
        }
    }
    const MgTouch *tt = k->touch >= 0 ? &ui->in->touches[k->touch] : NULL;
    if (k->drag && (clicked || !tt || !tt->active)) { /* released */
        if (k->drag_move < TAP_SLOP_PT && tt) { /* a tap on a slot: that item comes to the middle */
            const float d = (tt->y - cy) / l->slot;
            const int target = (int)clampf(floorf(k->pos[tab] + d + 0.5f), 0.0f, (float)(n - 1));
            k->pos[tab] = k->reduced ? (float)target : k->pos[tab];
            k->vel = 0.0f;
            *field(skin, tab) = (uint8_t)target;
            *changed = true;
            if (ui->fx) {
                ui_fx_sound(ui->fx, UI_SND_TICK, 0);
            }
        } else {
            k->vel = k->reduced ? 0.0f : clampf(k->last_dy_rate, -30.0f, 30.0f);
        }
        k->drag = false;
        k->touch = -1;
        k->last_dy_rate = 0.0f;
    }
    if (!k->drag && app->input.wheel != 0.0f && app->input.mouse_seen && app->input.mouse_x >= l->wheel_x && app->input.mouse_x <= l->wheel_x + l->wheel_w) {
        const int target = (int)clampf((float)sel_of(k, tab) - (app->input.wheel > 0.0f ? 1.0f : -1.0f), 0.0f, (float)(n - 1)); /* desktop */
        *field(skin, tab) = (uint8_t)target;
        *changed = true;
    }
    if (k->demo_drag && !k->drag) { /* perf / pictures: a drag sweeping the whole list and a little past both ends */
        const float half = (float)(n - 1) * 0.5f;
        k->pos[tab] = half + (half + 0.3f) * sinf(k->t * 0.9f);
        k->vel = 1e-3f; /* "moving": the selection follows the middle below */
        k->drag_frames += 1.0f;
    } else if (!k->drag) { /* inertia, then the snap onto the selected item */
        k->pos[tab] += k->vel * dt;
        k->vel *= expf(-FRICTION * dt);
        if (k->pos[tab] < -0.45f || k->pos[tab] > (float)(n - 1) + 0.45f) {
            k->vel = 0.0f;
        }
        if (fabsf(k->vel) < 2.5f) {
            k->vel = 0.0f;
            const float goal = (float)field_c(skin, tab);
            k->pos[tab] = k->reduced ? goal : goal + (k->pos[tab] - goal) * expf(-SNAP_RATE * dt);
            if (fabsf(k->pos[tab] - goal) < 1e-3f) {
                k->pos[tab] = goal;
            }
        }
    }
    const int sel = sel_of(k, tab);
    if (sel != k->shown_sel) { /* a detent: the middle slot shows another item */
        if (k->shown_sel >= 0) {
            ui_haptic(HAPTIC_LOCKER_DETENT);
            if (ui->fx) {
                ui_fx_sound(ui->fx, UI_SND_TICK, 0);
            }
        }
        k->shown_sel = sel;
        if ((k->drag || k->vel != 0.0f) && field_c(skin, tab) != sel) { /* passing through: the selection follows the middle */
            *field(skin, tab) = (uint8_t)sel;
            *changed = true;
        }
    }
    /* tests and taps by id: every slot on screen, absolute (cr<tab>_0_<i>) and relative to the middle (crslot+1 ..) */
    for (int d = -1; d <= 1; d++) { /* the slots inside the wheel (WHEEL_REACH) */
        const int i = sel + d;
        if (i < 0 || i >= n) {
            continue;
        }
        const float y = cy + ((float)i - k->pos[tab]) * l->slot - l->slot * 0.5f;
        char id[24];
        SDL_snprintf(id, sizeof(id), "cr%d_0_%d", tab, i);
        ui_remember(ui, id, l->wheel_x, y, l->wheel_w, l->slot);
        if (d != 0) {
            SDL_snprintf(id, sizeof(id), "crslot%+d", d);
            ui_remember(ui, id, l->wheel_x, y, l->wheel_w, l->slot);
        }
        if (d == (sel + 1 < n ? 1 : -1)) { /* tests: a neighbour that exists (the next one, or the previous at the end) */
            ui_remember(ui, "crnear", l->wheel_x, y, l->wheel_w, l->slot);
        }
    }
}

static const char *item_name(int tab, int i)
{
    switch (tab) {
    case 0: return str(i == 0 ? "male" : "female");
    case 2: return avatar_hair_name(i);
    case 4: return avatar_suit_name(i);
    case 5: return avatar_accessory_name(i);
    default: return NULL; /* colours: the model says it */
    }
}

static void hero_input(Locker *k, MgApp *app, const LockerLayout *l)
{
    const float dt = mg_minf(app->frame_dt, 0.1f);
    bool dragging = false;
    for (int i = 0; i < MG_MAX_TOUCHES; i++) {
        MgTouch *t = &app->input.touches[i];
        if (t->active && t->owner == MG_TOUCH_FREE && t->x0 >= l->stage_x && t->x0 <= l->stage_x + l->stage_w && t->y0 >= l->top && !t->began) {
            k->yaw += HMM_AngleDeg(TURN_DEG_PER_PT) * t->dx;
            k->yaw_vel = HMM_AngleDeg(TURN_DEG_PER_PT) * t->dx / mg_maxf(dt, 1e-3f) * 0.3f;
            dragging = true;
        }
    }
    k->hero_drag = dragging;
    if (!dragging) {
        k->yaw += k->yaw_vel * dt; /* the release eases out */
        k->yaw_vel *= expf(-3.0f * dt);
    }
    /* the idle ping-pong: its weight drops while dragging and comes back once still; its clock runs only while it shows,
     * so it resumes from where it left off (no jump) */
    const bool still = !dragging && fabsf(k->yaw_vel) < 0.05f;
    k->swing_w = k->reduced ? 0.0f : still ? mg_minf(k->swing_w + dt / SWING_RETURN_S, 1.0f) : mg_maxf(k->swing_w - dt * 4.0f, 0.0f);
    k->swing_t += dt * k->swing_w;
}

bool locker_ui(Locker *k, Ui *ui, MgApp *app, NetSkin *skin, bool show_back, bool *done, bool *back)
{
    LockerLayout l;
    locker_layout(app, &l);
    *done = *back = false;
    if (!k->open) {
        k->open = true;
        k->t = 0.0f;
        k->enter = 0.0f;
        k->shown_sel = -1;
        for (int t = 0; t < LOCKER_TABS; t++) {
            k->pos[t] = (float)field_c(skin, t);
        }
    }
    k->reduced = ui->anim && ui->anim->reduced;
    k->t += mg_minf(app->frame_dt, 0.1f);
    bool changed = false;
    /* top left: back (not in onboarding) and the dice; the title sits on the backdrop card's corner */
    float x = l.tabs_x, y = l.top;
    float ix = x;
    if (show_back) {
        if (ui_icon_button(ui, "feback", UI_ICON_BACK, ix, y, k_ui.icon_button, UI_V_SECONDARY)) {
            *back = true;
        }
        ix += k_ui.icon_button + 10.0f;
    }
    if (ui_icon_button_f(ui, "dice", UI_ICON_DICE, ix, y, k_ui.icon_button, UI_V_SECONDARY, UI_F_RANDOMISE)) {
        creator_randomise(skin);
        changed = true;
    }
    if (k->stage > 0.5f) {
        ui_text(ui, TEXT_FACE_DISPLAY, l.card_x + 20.0f, l.card_y + 12.0f, 28.0f, ui_rgba(k_ui.ink), str("locker"));
    }
    /* the tabs under them */
    y += k_ui.icon_button + 6.0f;
    const float room = l.bottom - y;
    const float th = clampf(room / (float)LOCKER_TABS - 4.0f, k_ui.target_min, 50.0f);
    for (int t = 0; t < LOCKER_TABS; t++) { /* stacked buttons, as the Friends page's tabs */
        char label[64];
        SDL_snprintf(label, sizeof(label), "%s##crtab%d", str(k_tab_keys[t]), t);
        ui->hclass = HC_TAB;
        if (ui_button_v(ui, label, x, y, l.tabs_w, th, k->tab == t ? UI_V_PRIMARY : UI_V_SECONDARY, UI_F_SMALL) && k->tab != t) {
            k->tab = t;
            k->vel = 0.0f;
            k->drag = false;
            k->shown_sel = -1;
            k->pos[t] = (float)field_c(skin, t);
        }
        y += th + 4.0f;
    }
    /* right: the wheel, then Done */
    wheel_input(k, ui, app, &l, skin, &changed);
    { /* the selection frame round the middle slot (UI over the 3D card), with a chevron on each side */
        const float cy = l.wheel_y + l.wheel_h * 0.5f, s = l.slot;
        ui_depth(ui, 2, 0, 0);
        ui_shape(ui->gfx, &(UiShape){.x = l.wheel_x - 3.0f, .y = cy - s * 0.5f - 3.0f, .w = l.wheel_w + 6.0f, .h = s + 6.0f, .radius = k_ui.r_row + 3.0f,
                                     .stroke = k_ui.ring_width, .color = ui_rgba(k_ui.primary_lip)});
        ui_depth_end(ui);
    }
    char label[48];
    SDL_snprintf(label, sizeof(label), "%s##creator", str("done"));
    if (ui_button_v(ui, label, l.wheel_x, l.buttons_y, l.wheel_w, k_ui.target_min, UI_V_PRIMARY, UI_F_SMALL | UI_F_BREATHE)) {
        *done = true;
    }
    /* the item's name, on a pill under the hero */
    const char *name = item_name(k->tab, field_c(skin, k->tab));
    if (name && name[0]) {
        const float nw = ui_text_w(ui, TEXT_FACE_BOLD, name, k_ui.t_body) + 28.0f, nh = 32.0f;
        const float nx = l.stage_x + (l.stage_w - nw) * 0.5f, ny = l.bottom - nh - 10.0f;
        ui_surface(ui, nx, ny, nw, nh - 4.0f, -1.0f, 4.0f, ui_rgba(k_ui.surface_tint), ui_rgba(k_ui.secondary_lip), 0.0f, ui_rgba(0));
        ui_text_c(ui, TEXT_FACE_BOLD, nx + nw * 0.5f, ny + (nh - 4.0f) * 0.5f, k_ui.t_body, ui_rgba(k_ui.ink), name);
    }
    hero_input(k, app, &l);
    return changed;
}

void locker_close(Locker *k)
{
    k->open = false;
    k->drag = false;
    k->touch = -1;
}

/* a rounded card in the stage inset (pt space, y down): 2 quads + 4 discs, all instanced */
static void card(Locker *k, GfxContext *g, float x, float y, float w, float h, float r, float z, HMM_Vec4 c, float hide)
{
    GfxMaterial m = mat_unlit(c);
    m.hide = hide;
    r = mg_minf(r, mg_minf(w, h) * 0.5f);
    const HMM_Mat4 a = HMM_MulM4(HMM_Translate(HMM_V3(x + r, y, z)), HMM_Scale(HMM_V3(w - 2.0f * r, h, 1.0f)));
    const HMM_Mat4 b = HMM_MulM4(HMM_Translate(HMM_V3(x, y + r, z)), HMM_Scale(HMM_V3(w, h - 2.0f * r, 1.0f)));
    gfx_draw(g, k->quad, &a, &m, GFX_DRAW_NO_SHADOW);
    gfx_draw(g, k->quad, &b, &m, GFX_DRAW_NO_SHADOW);
    const float cx[4] = {x + r, x + w - r, x + r, x + w - r}, cy[4] = {y + r, y + r, y + h - r, y + h - r};
    for (int i = 0; i < 4; i++) {
        const HMM_Mat4 d = HMM_MulM4(HMM_Translate(HMM_V3(cx[i], cy[i], z)), HMM_Scale(HMM_V3(r, r, 1.0f)));
        gfx_draw(g, k->disc, &d, &m, GFX_DRAW_NO_SHADOW);
    }
}

static HMM_Vec4 tok(uint32_t rgba) { return mg_rgb(rgba >> 8); }

/* what of a standing avatar is drawn, in metres above its feet (it floats: the body base is above them), for framing */
static void avatar_extent(const AvatarMeshes *m, const AvatarLook *look, float *bottom, float *top, float *head_r)
{
    AvatarPose p;
    SDL_zero(p);
    p.grounded = true;
    AvatarAnim a;
    SDL_zero(a);
    AvatarSolved s;
    avatar_solve(m, look, &p, &a, &s);
    *head_r = m->rig.head_r * s.scale;
    *top = s.head_centre.Y + *head_r * 1.25f; /* hair and hats */
    *bottom = s.base_y - 0.05f * *top;        /* the suit's hem under the body base */
}

float locker_hero_yaw(const Locker *k)
{
    return k->yaw + HMM_AngleDeg(SWING_DEG) * sinf(2.0f * HMM_PI32 * k->swing_t / SWING_PERIOD_S) * k->swing_w;
}

void locker_draw(Locker *k, GfxContext *g, const MgApp *app, const AvatarMeshes *m, const NetSkin *skin, float cam_t, bool own_hero, float dt)
{
    const float goal = clampf((cam_t - STAGE_FROM) / (1.0f - STAGE_FROM), 0.0f, 1.0f);
    k->stage = goal; /* follows the camera (its own ease), so it reverses with it */
    k->enter = k->open ? (k->reduced ? 1.0f : mg_minf(k->enter + dt / ENTER_S, 1.0f)) * (k->stage > 0.0f ? 1.0f : 0.0f) : mg_maxf(k->enter - dt / ENTER_S, 0.0f);
    if (k->stage <= 0.0f) {
        return;
    }
    LockerLayout l;
    locker_layout(app, &l);
    const float W = app->width_pt, H = app->height_pt;
    const float fade = 1.0f - k->stage; /* screen-door: 1 = gone */
    /* 1: the stage, in pt (x right, y down), ortho; z: nearer is larger. Drawn ahead of the world at the far end of the depth range:
     * behind everything, so the library's hero stands in front of it while the scenery round it dithers away (gfx_set_hide_min) */
    {
        const HMM_Mat4 ortho = HMM_Orthographic_RH_ZO(0.0f, W, H, 0.0f, -10.0f, 10.0f);
        gfx_inset_begin(g, 0.0f, 0.0f, W, H, &ortho, HMM_V3(W * 0.5f, H * 0.5f, 10.0f), 0.9990f, 0.9998f, true);
        const HMM_Mat4 fill = HMM_MulM4(HMM_Translate(HMM_V3(0, 0, -5.0f)), HMM_Scale(HMM_V3(W, H, 1.0f)));
        GfxMaterial fm = mat_unlit(tok(k_ui.surface_tint));
        fm.hide = fade;
        gfx_draw(g, k->quad, &fill, &fm, GFX_DRAW_NO_SHADOW);
        /* the backdrop card: enters with a small overshoot (the Jelly Pop enter curve: scale 0.92 -> 1) */
        const float e = k->enter, s = 0.92f + 0.08f * (1.0f - powf(1.0f - e, 3.0f)) + 0.03f * sinf(e * HMM_PI32) * (1.0f - e);
        const float cw = l.card_w * s, ch = l.card_h * s, cx = l.card_x + (l.card_w - cw) * 0.5f, cy = l.card_y + (l.card_h - ch) * 0.5f;
        const float hide_card = mg_maxf(fade, 1.0f - e);
        card(k, g, cx, cy + k_ui.lip_card, cw, ch, k_ui.r_card, -2.0f, tok(k_ui.lip_mint), hide_card);
        card(k, g, cx, cy, cw, ch, k_ui.r_card, -1.0f, tok(k_ui.surface), hide_card);
        /* the wheel's cards: one per slot on screen, shrinking towards the ends */
        const int tab = k->tab, n = k_counts[tab];
        const float wcy = l.wheel_y + l.wheel_h * 0.5f;
        for (int i = 0; i < n; i++) {
            const float d = (float)i - k->pos[tab];
            if (fabsf(d) > WHEEL_REACH) {
                continue; /* not on screen: not drawn */
            }
            const float sc = 1.0f - 0.12f * fabsf(d), sw = (l.wheel_w - 8.0f) * sc, sh = (l.slot - 8.0f) * sc;
            const float y = wcy + d * l.slot - sh * 0.5f, x = l.wheel_x + (l.wheel_w - sw) * 0.5f;
            const float hide = mg_maxf(k->open ? fade : 1.0f, clampf((fabsf(d) - 1.0f) / (WHEEL_REACH - 1.0f), 0.0f, 1.0f)) /* closed: gone with its UI */;
            card(k, g, x, y + k_ui.lip_row, sw, sh - k_ui.lip_row, k_ui.r_row, -1.5f, tok(k_ui.secondary_lip), hide);
            card(k, g, x, y, sw, sh - k_ui.lip_row, k_ui.r_row, -0.5f, tok(i == field_c(skin, tab) ? k_ui.surface : k_ui.surface_tint), hide);
        }
        gfx_inset_end(g);
    }
    /* 2: the hero of its own, standing, perspective, turning: only with no library behind (the Locker opened from a game); in the
     * menu the hero is the library's own (seat 0), turned by locker_hero_yaw */
    const AvatarLook look = avatar_look_from_skin(*skin);
    if (own_hero) {
        float bottom, top, head_r;
        avatar_extent(m, &look, &bottom, &top, &head_r);
        const float sx = l.stage_x, sw = l.stage_w, sy = l.top, sh = l.bottom - l.top - 44.0f; /* the name pill under it */
        const float mid = (top + bottom) * 0.5f;
        const float fov = HMM_AngleDeg(24.0f), span = (top - bottom) * mg_maxf(1.25f, 1.25f * sh / 330.0f), dist = span * 0.5f / tanf(fov * 0.5f);
        const HMM_Vec3 at = HMM_V3(0.0f, mid, 0.0f), eye = HMM_V3(0.0f, mid + 0.08f * span, dist);
        const HMM_Mat4 vp = HMM_MulM4(HMM_Perspective_RH_ZO(fov, sw / mg_maxf(sh, 1.0f), 0.1f, dist + 4.0f), HMM_LookAt_RH(eye, at, HMM_V3(0, 1, 0)));
        gfx_inset_begin(g, sx, sy, sw, sh, &vp, eye, 0.01f, 0.02f, false);
        AvatarPose p;
        SDL_zero(p);
        p.grounded = true;

        p.yaw = HMM_PI32 + locker_hero_yaw(k);
        p.hide = fade;
        avatar_animate(m, &k->hero_anim, &p, dt);
        avatar_draw(g, m, &look, &p, &k->hero_anim, NULL);
        gfx_inset_end(g);
        (void)head_r;
    }
    /* 3: the wheel's models, orthographic, metres; only the slots on screen */
    {
        const int tab = k->tab, n = k_counts[tab];
        float bottom, top, head_r;
        avatar_extent(m, &look, &bottom, &top, &head_r);
        const bool head = k_head_only[tab];
        const float item_m = head ? head_r * 2.9f : (top - bottom) * 1.08f; /* what one slot shows, in metres */
        const float ppm = (l.slot - 14.0f) / item_m;           /* pt per metre */
        const float hw = l.wheel_w * 0.5f / ppm, hh = l.wheel_h * 0.5f / ppm;
        const HMM_Mat4 ortho = HMM_Orthographic_RH_ZO(-hw, hw, -hh, hh, -10.0f, 10.0f);
        const HMM_Mat4 vp = HMM_MulM4(ortho, HMM_LookAt_RH(HMM_V3(0, 0, 5.0f), HMM_V3(0, 0, 0), HMM_V3(0, 1, 0)));
        gfx_inset_begin(g, l.wheel_x, l.wheel_y, l.wheel_w, l.wheel_h, &vp, HMM_V3(0, 0, 5.0f), 0.0f, 0.01f, false);
        const int sel = sel_of(k, tab);
        for (int j = 0; j < LOCKER_SHOWN; j++) {
            const int i = sel - LOCKER_SHOWN / 2 + j;
            if (i < 0 || i >= n) {
                continue;
            }
            const float d = (float)i - k->pos[tab];
            if (fabsf(d) > WHEEL_REACH) {
                continue;
            }
            NetSkin s = *skin;
            *field(&s, tab) = (uint8_t)i;
            const AvatarLook il = avatar_look_from_skin(s);
            AvatarPose p;
            SDL_zero(p);
            p.grounded = true;

            p.head_only = head;
            p.scale = 1.0f - 0.12f * fabsf(d);
            const float yc = -d * l.slot / ppm; /* slot centre, metres (y up) */
            p.feet = head ? HMM_V3(0.0f, yc, 0.0f) : HMM_V3(0.0f, yc - (top + bottom) * 0.5f * p.scale, 0.0f);
            p.yaw = HMM_PI32 + HMM_AngleDeg(20.0f) * sinf(k->t * 1.3f + (float)i); /* a little turn so the shape reads */
            p.hide = mg_maxf(k->open ? fade : 1.0f, clampf((fabsf(d) - 1.0f) / (WHEEL_REACH - 1.0f), 0.0f, 1.0f)) /* closed: gone with its UI */;
            avatar_animate(m, &k->item_anim[j], &p, dt);
            avatar_draw(g, m, &il, &p, &k->item_anim[j], NULL);
        }
        gfx_inset_end(g);
    }
}
