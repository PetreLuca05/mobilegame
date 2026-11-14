/* UI-1 "Jelly Pop": the one token table. Every colour, size, radius, duration, curve and
 * effect number the UI uses lives here; screens and widgets take them from k_ui. Points (pt)
 * at 1x UI scale; the iPad scale applies on top. Colours are 0xRRGGBBAA. */
#ifndef MG_UI_STYLE_H
#define MG_UI_STYLE_H

#include <stdint.h>

typedef struct UiBezier {
    float x1, y1, x2, y2; /* CSS cubic-bezier control points */
} UiBezier;

typedef struct UiStyleTable {
    /* colour */
    uint32_t ink, ink_soft, teal_text;
    uint32_t surface, surface_tint, lip_mint;
    uint32_t primary, primary_lip, secondary_lip;
    uint32_t accent, accent_lip, fill, off;
    uint32_t disabled, disabled_lip, disabled_text;
    uint32_t ok, ok_lip, bad, bad_lip;
    uint32_t coin;
    uint32_t white, white_soft, ink_lip, hairline, glass; /* text on colour; version; the ink pill's lip; dividers; chip on the 3D */
    uint32_t status_off, status_menu, status_game;     /* friends' presence dots */
    uint32_t brand_google;
    uint32_t google_face, google_lip, google_text, apple_face, apple_lip, apple_text; /* sign-in buttons in the providers' colours */
    uint32_t lift_display_bg, lift_display_digit;      /* the lift's floor display: the one dark plate (an elevator display) */
    uint32_t confetti[4];
    uint32_t scrim;
    uint32_t chapter[6];                  /* BOOK-ENGINE: a chapter's colour (objective marker, pill strip, journal), by arc row */
    uint32_t highlight, highlight_accent; /* button top band: white 55 %, 30 % on accent */
    uint32_t fill_highlight;              /* progress bar top band: white 35 % */
    /* shape */
    float lip_button, lip_card, lip_pill, lip_row;
    float r_button, r_card, r_tab, r_row;
    float highlight_band, fill_highlight_band, ring_width, icon_stroke, icon_box;
    float target_min, button_h, icon_button, row_h;
    float edge_secondary; /* a secondary (white) button's outline in secondary_lip: its top edge on white cards */
    float tag_h, tag_pad, tag_lip; /* a small status chip (ui_tag_chip): height, text inset, lip */
    float toggle_w, toggle_h, toggle_knob, progress_h;
    /* type (pt) */
    float t_title, t_button, t_body, t_small, t_label, label_tracking_em;
    /* motion (s) */
    float press_s, release_s, spring_s, fill_s, enter_s, exit_s, exit_fade_s, stagger_s, bump_s, pop_s, shake_s;
    float breathe_s, bob_s, colour_s;
    int stagger_max;
    float press_sx, press_sy, bump_scale, pop_scale, breathe_scale, bob_y, bob_tilt_deg;
    float release_t[5], release_sx[5], release_sy[5];
    float shake_x[8];
    UiBezier spring, enter;
    float reduced_press, reduced_s;
    /* effects */
    int particles, rings, tokens;
    float dot_size, burst_spread_rad, burst_d_min, burst_d_max, burst_life_min_s, burst_life_max_s, burst_peak_t, burst_peak_scale, burst_fall;
    UiBezier burst;
    float ring_size, ring_stroke, ring_s, ring_from, ring_to;
    float token_size, token_gap_s, token_s, token_pop_t, token_pop_scale, token_end_scale, token_dx, token_dy_min, token_dy_max;
    float banner_size, banner_radius, banner_lip, banner_s;
    /* UI-1b stick (pt at R = 75; scaled by R / 75) */
    uint32_t stick_base, stick_border, stick_lip, stick_ring, stick_dot, stick_full, stick_full_fill;
    float stick_design_r, stick_lip_off, stick_border_w, stick_ring_at, stick_ring_w, stick_ring_full_w, stick_ring_dashes, stick_dot_d;
    float stick_knob_d, stick_knob_lip, stick_knob_hl;
    float stick_idle_alpha, stick_in_s, stick_pop_s, stick_pop_from, stick_pop_peak, stick_stretch_along, stick_stretch_across;
    float stick_rearm_at, stick_tick_gap_s, stick_release_s, stick_fade_s, stick_hide_s, stick_reduced_s;
    float stick_rel_t[5], stick_rel_k[5], stick_rel_along[5], stick_rel_across[5];
    /* TILT-1 (signs: +1 / -1 per axis, flipped here if the hand check finds an axis inverted) */
    float tilt_sign_x, tilt_sign_y, tilt_dead_deg, tilt_range_deg, tilt_ref_s, tilt_spring_hz, tilt_spring_damping;
    float tilt_still_dps, tilt_still_s, tilt_rest_pt, tilt_ceiling_s, tilt_backstop_s; /* A1: the rest rule, the ceiling, the backstop */
    float tilt_shake_speed, tilt_shake_gap_s, tilt_jiggle_stagger_s;
    float tilt_depth[5];                 /* max shift (pt) per depth 0..4 */
    float tilt_lip_x, tilt_lip_y;        /* lip swing: (-0.7 L tx, -0.4 L ty) from rest */
    float tilt_highlight_pt, tilt_gloss_w, tilt_gloss_alpha, tilt_gloss_travel, tilt_title_lip, tilt_hud, tilt_edge_pt;
    float tilt_cam_yaw_deg, tilt_cam_pitch_deg;
    /* sound: gain, pitch jitter */
    float sound_gain, sound_jitter;
} UiStyleTable;

static const UiStyleTable k_ui = {
    .ink = 0x173A3AFF, .ink_soft = 0x2A5753FF, .teal_text = 0x0B6B62FF,
    .surface = 0xFFFFFFFF, .surface_tint = 0xEAF7F3FF, .lip_mint = 0xB5E6D8FF,
    .primary = 0xFFC933FF, .primary_lip = 0xD99A00FF, .secondary_lip = 0xA9DDD0FF,
    .accent = 0x0E8F83FF, .accent_lip = 0x0A6A61FF, .fill = 0x12A594FF, .off = 0x9DB8B2FF,
    .disabled = 0xE3EFECFF, .disabled_lip = 0xC5D8D3FF, .disabled_text = 0x4F6965FF,
    .ok = 0x1A7F45FF, .ok_lip = 0x0F5A2FFF, .bad = 0xC93A3AFF, .bad_lip = 0x8E2424FF,
    .coin = 0xF2A900FF,
    .white = 0xFFFFFFFF, .white_soft = 0xFFFFFFA0, .ink_lip = 0x0D2626FF, .hairline = 0xFFFFFF22, .glass = 0xFFFFFF26,
    .status_off = 0x8A8F98FF, .status_menu = 0x3B82F6FF, .status_game = 0x34C759FF,
    .brand_google = 0x4285F4FF,
    .google_face = 0xFFFFFFFF, .google_lip = 0xDADCE0FF, .google_text = 0x1F1F1FFF,
    .apple_face = 0x000000FF, .apple_lip = 0x3A3A3CFF, .apple_text = 0xFFFFFFFF,
    .lift_display_bg = 0x14171FFF, .lift_display_digit = 0xFFD166FF,
    .confetti = {0xFFC933FF, 0xFF7A6BFF, 0x12A594FF, 0xFFFFFFFF},
    .scrim = 0x173A3A59, /* 35 % */
    .chapter = {0xFF7A6BFF, 0x4A8BFFFF, 0x12A594FF, 0xB46BFFFF, 0xF2A900FF, 0xFF6BB5FF},
    .highlight = 0xFFFFFF8C, .highlight_accent = 0xFFFFFF4D, .fill_highlight = 0xFFFFFF59,
    .lip_button = 7.0f, .lip_card = 7.0f, .lip_pill = 5.0f, .lip_row = 4.0f,
    .r_button = 22.0f, .r_card = 26.0f, .r_tab = 18.0f, .r_row = 16.0f,
    .highlight_band = 4.0f, .fill_highlight_band = 5.0f, .ring_width = 3.0f, .icon_stroke = 2.4f, .icon_box = 24.0f,
    .target_min = 44.0f, .button_h = 54.0f, .icon_button = 54.0f, .row_h = 50.0f, .edge_secondary = 1.5f,
    .tag_h = 24.0f, .tag_pad = 10.0f, .tag_lip = 3.0f,
    .toggle_w = 76.0f, .toggle_h = 44.0f, .toggle_knob = 36.0f, .progress_h = 22.0f,
    .t_title = 40.0f, .t_button = 22.0f, .t_body = 16.0f, .t_small = 14.0f, .t_label = 12.0f, .label_tracking_em = 0.14f,
    .press_s = 0.09f, .release_s = 0.48f, .spring_s = 0.45f, .fill_s = 0.70f, .enter_s = 0.55f, .exit_s = 0.18f, .exit_fade_s = 0.08f,
    .stagger_s = 0.04f, .bump_s = 0.20f, .pop_s = 0.38f, .shake_s = 0.32f, .breathe_s = 2.4f, .bob_s = 2.2f, .colour_s = 0.20f,
    .stagger_max = 8,
    .press_sx = 1.07f, .press_sy = 0.86f, .bump_scale = 1.22f, .pop_scale = 1.05f, .breathe_scale = 1.03f, .bob_y = -3.0f, .bob_tilt_deg = 8.0f,
    .release_t = {0.0f, 0.30f, 0.55f, 0.78f, 1.0f},
    .release_sx = {1.07f, 0.93f, 1.04f, 0.99f, 1.0f},
    .release_sy = {0.86f, 1.12f, 0.96f, 1.02f, 1.0f},
    .shake_x = {0.0f, -1.0f, 1.0f, -0.7f, 0.7f, -0.35f, 0.35f, 0.0f},
    .spring = {0.3f, 1.8f, 0.5f, 1.0f},
    .enter = {0.3f, 1.6f, 0.5f, 1.0f},
    .reduced_press = 0.96f, .reduced_s = 0.15f,
    .particles = 64, .rings = 4, .tokens = 8,
    .dot_size = 12.0f, .burst_spread_rad = 0.6f, .burst_d_min = 0.55f, .burst_d_max = 1.15f, .burst_life_min_s = 0.65f, .burst_life_max_s = 0.85f,
    .burst_peak_t = 0.35f, .burst_peak_scale = 1.2f, .burst_fall = 40.0f,
    .burst = {0.15f, 0.7f, 0.3f, 1.0f},
    .ring_size = 90.0f, .ring_stroke = 6.0f, .ring_s = 0.46f, .ring_from = 0.2f, .ring_to = 2.2f,
    .token_size = 26.0f, .token_gap_s = 0.06f, .token_s = 0.72f, .token_pop_t = 0.30f, .token_pop_scale = 1.25f, .token_end_scale = 0.6f,
    .token_dx = 70.0f, .token_dy_min = -100.0f, .token_dy_max = -30.0f,
    .banner_size = 84.0f, .banner_radius = 40.0f, .banner_lip = 12.0f, .banner_s = 1.7f,
    .stick_base = 0xFFFFFF99, .stick_border = 0xFFFFFFFF, .stick_lip = 0x173A3A2E, .stick_ring = 0x173A3A4D, .stick_dot = 0x173A3A40,
    .stick_full = 0x0E8F83FF, .stick_full_fill = 0x0E8F8329,
    .stick_design_r = 75.0f, .stick_lip_off = 6.0f, .stick_border_w = 3.0f, .stick_ring_at = 0.75f, .stick_ring_w = 2.0f, .stick_ring_full_w = 3.0f,
    .stick_ring_dashes = 24.0f, .stick_dot_d = 8.0f, .stick_knob_d = 68.0f, .stick_knob_lip = 7.0f, .stick_knob_hl = 4.0f,
    .stick_idle_alpha = 0.55f, .stick_in_s = 0.08f, .stick_pop_s = 0.26f, .stick_pop_from = 0.8f, .stick_pop_peak = 1.05f,
    .stick_stretch_along = 0.12f, .stick_stretch_across = 0.08f,
    .stick_rearm_at = 0.70f, .stick_tick_gap_s = 0.4f, .stick_release_s = 0.42f, .stick_fade_s = 0.2f, .stick_hide_s = 0.15f, .stick_reduced_s = 0.1f,
    .stick_rel_t = {0.0f, 0.35f, 0.62f, 0.82f, 1.0f},
    .stick_rel_k = {1.0f, -0.2f, 0.08f, -0.02f, 0.0f},
    .stick_rel_along = {1.0f, 0.94f, 1.03f, 1.0f, 1.0f},
    .stick_rel_across = {1.0f, 1.08f, 0.97f, 1.0f, 1.0f},
    .tilt_sign_x = 1.0f, .tilt_sign_y = 1.0f, .tilt_dead_deg = 1.0f, .tilt_range_deg = 16.0f, /* A1 said 12; 16 (approved) gives its "20 deg in 0.3 s peaks near 0.7" */ .tilt_ref_s = 0.35f, .tilt_spring_hz = 4.0f,
    .tilt_spring_damping = 0.7f, .tilt_still_dps = 3.0f, .tilt_still_s = 0.15f, .tilt_rest_pt = 0.25f, .tilt_ceiling_s = 1.5f, .tilt_backstop_s = 4.0f, .tilt_shake_speed = 6.0f, .tilt_shake_gap_s = 2.0f, .tilt_jiggle_stagger_s = 0.015f,
    .tilt_depth = {0.0f, 4.0f, 7.0f, 10.0f, 14.0f},
    .tilt_lip_x = 0.7f, .tilt_lip_y = 0.4f,
    .tilt_highlight_pt = 2.0f, .tilt_gloss_w = 0.4f, .tilt_gloss_alpha = 0.18f, .tilt_gloss_travel = 0.3f, .tilt_title_lip = 1.5f, .tilt_hud = 0.5f,
    .tilt_edge_pt = 14.0f, .tilt_cam_yaw_deg = 1.2f, .tilt_cam_pitch_deg = 0.8f,
    .sound_gain = 0.07f, .sound_jitter = 0.03f,
};

#endif
