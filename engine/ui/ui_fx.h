/* UI-1 feedback: fixed pools of burst particles (64), rings (4) and reward tokens (8), the
 * completion banner and the UI shake, the procedural UI sounds and haptics, and the tiers that
 * tie them to UI events (T0 .. T3, T2-, N). All drawn as instances in the one UI draw. */
#ifndef MG_UI_FX_H
#define MG_UI_FX_H

#include "audio/audio.h"
#include "platform/haptics.h"
#include "text.h"
#include "ui_anim.h"

typedef enum UiSound { UI_SND_DOWN, UI_SND_TAP, UI_SND_TICK, UI_SND_TOGGLE_ON, UI_SND_TOGGLE_OFF, UI_SND_COIN, UI_SND_SUCCESS, UI_SND_ERROR,
                       UI_SND_NOTICE, UI_SND_FANFARE } UiSound;
typedef enum UiTier { UI_T0, UI_T1, UI_T1_TAB, UI_T1_TOGGLE, UI_T2, UI_T2_FAIL, UI_T3, UI_TN } UiTier;

typedef struct UiParticle {
    float x, y, dx, dy, life, size;
    double t0;
    uint32_t color;
    bool on;
} UiParticle;

typedef struct UiRing {
    float x, y;
    double t0;
    bool on;
} UiRing;

typedef struct UiToken {
    float x0, y0, mx, my;
    double t0;
    int index; /* 0..7 in its flight */
    bool on, landed;
} UiToken;

typedef struct UiFx {
    UiParticle parts[64];
    UiRing rings[4];
    UiToken tokens[8];
    /* the reward in flight: its counter and what has landed */
    uint32_t counter_key;
    float counter_x, counter_y;
    int amount, landed_amount, landed_tokens, flight_tokens;
    double countup_t0;     /* reduced motion: the number counts up instead */
    bool countup;
    char banner[48];
    double banner_t0;
    double shake_t0;
    float shake_amp;
    uint32_t rng;
    UiAnim *anim;
    Audio *audio;
    const TextFont *display;
    bool haptics, sounds;
    int peak_particles, peak_rings, peak_tokens;
    int sound_count, last_sound;   /* autotests */
    float last_pitch;
    int haptic_count;              /* autotests: haptics asked for (played only when enabled) */
    int coin_count;                /* autotests: coin ticks played and their nominal pitch */
    float coin_pitch[8];
} UiFx;

void ui_fx_init(UiFx *fx, UiAnim *anim, Audio *audio, const TextFont *display);
void ui_fx_sound(UiFx *fx, UiSound s, int index);
void ui_fx_haptic(UiFx *fx, HapticKind k); /* respects the haptics setting */
void ui_fx_burst(UiFx *fx, float cx, float cy, int n, float dist);
void ui_fx_ring(UiFx *fx, float cx, float cy);
void ui_fx_shake(UiFx *fx, float amp);
/* Eight tokens from (fx, fy) into the counter at (cx, cy) keyed by counter_key; each landing adds amount / 8. */
void ui_fx_reward(UiFx *fx, float fx_x, float fx_y, uint32_t counter_key, float cx, float cy, int amount);
/* What the counter shows while a reward flies: its final value minus what has not landed yet. */
int ui_fx_shown(const UiFx *fx, uint32_t counter_key, int final_value);
void ui_fx_banner(UiFx *fx, const char *text);
/* One UI event: motion is the widget's, this adds effects, sound and haptic per the tier table. */
void ui_fx_tier(UiFx *fx, UiTier tier, float cx, float cy, bool on);
/* The UI shake offset for this frame (UI only, never the 3D camera). */
float ui_fx_shake_x(const UiFx *fx);
void ui_fx_update_draw(UiFx *fx, GfxContext *g, float w_pt, float h_pt);
int ui_fx_live(const UiFx *fx, int *particles, int *rings, int *tokens);

#endif
