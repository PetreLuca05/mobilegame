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
    uint32_t confetti[4];
    uint32_t scrim;
    uint32_t highlight, highlight_accent; /* button top band: white 55 %, 30 % on accent */
    uint32_t fill_highlight;              /* progress bar top band: white 35 % */
    /* shape */
    float lip_button, lip_card, lip_pill, lip_row;
    float r_button, r_card, r_tab, r_row;
    float highlight_band, fill_highlight_band, ring_width, icon_stroke, icon_box;
    float target_min, button_h, icon_button, row_h;
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
    .confetti = {0xFFC933FF, 0xFF7A6BFF, 0x12A594FF, 0xFFFFFFFF},
    .scrim = 0x173A3A59, /* 35 % */
    .highlight = 0xFFFFFF8C, .highlight_accent = 0xFFFFFF4D, .fill_highlight = 0xFFFFFF59,
    .lip_button = 7.0f, .lip_card = 7.0f, .lip_pill = 5.0f, .lip_row = 4.0f,
    .r_button = 22.0f, .r_card = 26.0f, .r_tab = 18.0f, .r_row = 16.0f,
    .highlight_band = 4.0f, .fill_highlight_band = 5.0f, .ring_width = 3.0f, .icon_stroke = 2.4f, .icon_box = 24.0f,
    .target_min = 44.0f, .button_h = 54.0f, .icon_button = 54.0f, .row_h = 50.0f,
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
    .sound_gain = 0.07f, .sound_jitter = 0.03f,
};

#endif
