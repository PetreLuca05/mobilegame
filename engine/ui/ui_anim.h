/* UI-1 motion: a fixed pool of tweens (no allocation). A tween drives one property of one widget
 * (keyed by its id hash) with a cubic-bezier or a keyframe table. Starting a tween on a property
 * that is already moving starts from its current value; when a tween ends the property is back to
 * the rest value its widget passes in (unless it holds: press, held while the finger is down). */
#ifndef MG_UI_ANIM_H
#define MG_UI_ANIM_H

#include <stdbool.h>
#include <stdint.h>

#include "ui_style.h"

#define UI_TWEENS 128
#define UI_KEYS_MAX 8

typedef enum UiProp { UI_SX, UI_SY, UI_X, UI_Y, UI_ROT, UI_ALPHA, UI_VALUE, UI_PROP_COUNT } UiProp;
typedef enum UiEase { UI_EASE_LINEAR, UI_EASE_OUT, UI_EASE_IN, UI_EASE_IN_OUT, UI_EASE_BEZIER } UiEase;

typedef struct UiTween {
    uint32_t key;
    uint8_t prop, ease, nkeys;
    bool active, hold, loop;
    double seen;  /* loops: the last frame their widget asked for them */
    double t0;
    float dur;
    UiBezier bez;
    float t[UI_KEYS_MAX], v[UI_KEYS_MAX]; /* 2 keys = from -> to */
} UiTween;

typedef struct UiAnim {
    UiTween tw[UI_TWEENS];
    struct {
        uint32_t key;
        double seen;
    } marks[48];    /* ui_anim_fresh: widgets drawn last frame */
    double now;
    bool reduced;   /* ui_motion = 1 or iOS Reduce Motion */
    int peak_active; /* most tweens alive at once (autotests, perf) */
} UiAnim;

void ui_anim_begin(UiAnim *a, double now, bool reduced);
/* The property's value now, or `rest` when nothing drives it. */
float ui_anim_get(const UiAnim *a, uint32_t key, UiProp p, float rest);
bool ui_anim_busy(const UiAnim *a, uint32_t key, UiProp p);
/* From the current value (or rest) to `to` over dur seconds. */
void ui_anim_to(UiAnim *a, uint32_t key, UiProp p, float rest, float to, float dur, UiEase ease, const UiBezier *bez, bool hold);
/* A keyframe table (times 0..1, values); the first value is replaced by the current one. */
void ui_anim_keys(UiAnim *a, uint32_t key, UiProp p, float rest, const float *t, const float *v, int n, float dur, UiEase ease);
/* A loop between rest and `to` (breathe, bob): sine, period dur. */
void ui_anim_loop(UiAnim *a, uint32_t key, UiProp p, float rest, float to, float dur);
void ui_anim_stop(UiAnim *a, uint32_t key, UiProp p);
int ui_anim_count(const UiAnim *a);
/* The target of the tween on (key, p), if one runs or holds. */
bool ui_anim_target(const UiAnim *a, uint32_t key, UiProp p, float *to);
/* True on the first frame a widget is drawn (not drawn in the last 0.25 s); marks it as drawn now.
 * Loops (breathe, bob) whose widget is not drawn any more stop on their own. */
bool ui_anim_fresh(UiAnim *a, uint32_t key);
/* cubic-bezier(x1, y1, x2, y2) at x in 0..1 */
float ui_bezier(const UiBezier *b, float x);

#endif
