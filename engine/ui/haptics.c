#include "haptics.h"

#include "platform/haptics.h"

#define WINDOW_S 0.048 /* at most one start per 50 ms (2 ms slack: three 60 Hz frames are 50 ms, give or take rounding) */

/* The haptic engine is one per device, like the audio device: this module keeps its state. */
static struct {
    HapticLevel level;
    int forced;          /* -1 real capability */
    bool active;
    uint64_t frame;
    double now;
    double last_start, last_step;
    int pending;         /* this frame's strongest request, HAPTIC_NONE if none */
    float pending_k, pending_strength;
    HapticRecord ring[HAPTIC_RECORDS];
    uint64_t total;
} H = {HAPTIC_LEVEL_FULL, -1, true, 0, 0.0, -1e9, -1e9, HAPTIC_NONE, 1.0f, 0.0f, {{0}}, 0};

static float peak(const HapticEntry *e, int *at)
{
    float best = 0.0f;
    for (int i = 0; i < e->n; i++) {
        if (e->ev[i].intensity > best) {
            best = e->ev[i].intensity, *at = i;
        }
    }
    return best;
}

void ui_haptics_init(void)
{
    haptic_hw_init(k_haptics, HAPTIC_COUNT);
}

void ui_haptics_frame(uint64_t frame, double now)
{
    H.frame = frame, H.now = now;
}

void ui_haptics_active(bool active)
{
    H.active = active;
    haptic_hw_active(active);
}

/* record it and play it (the level scales the intensity) */
static void start(HapticId id, float k)
{
    const HapticEntry *e = &k_haptics[id];
    const float level = H.level == HAPTIC_LEVEL_FINE ? 0.6f : 1.0f;
    int at = 0;
    const float strength = peak(e, &at) * k * level;
    H.ring[H.total % HAPTIC_RECORDS] = (HapticRecord){(int16_t)id, strength, e->ev[at].sharpness, H.frame};
    H.total++;
    if (ui_haptic_supported()) {
        haptic_hw_play((int)id, k * level);
    }
}

void ui_haptic(HapticId id) { ui_haptic_scaled(id, 1.0f); }

void ui_haptic_scaled(HapticId id, float k)
{
    if (id < 0 || id >= HAPTIC_COUNT || !H.active || H.level == HAPTIC_LEVEL_OFF) {
        return;
    }
    const HapticEntry *e = &k_haptics[id];
    if (H.level == HAPTIC_LEVEL_FINE && e->rank <= 1) {
        return; /* Fin: nothing at R1 or below */
    }
    int at = 0;
    const float strength = peak(e, &at) * k;
    if (id == HAPTIC_SLIDER_STEP) { /* its own channel, at most 20 a second, so a step never blocks the end bump */
        if (H.now - H.last_step >= WINDOW_S) {
            H.last_step = H.now;
            start(id, k);
        }
        return;
    }
    if (H.pending == HAPTIC_NONE || strength > H.pending_strength) { /* a frame's requests: the strongest plays */
        H.pending = id, H.pending_k = k, H.pending_strength = strength;
    }
}

void ui_haptics_flush(void)
{
    if (H.pending == HAPTIC_NONE) {
        return;
    }
    if (H.now - H.last_start >= WINDOW_S) { /* one start per 50 ms; inside the window it is dropped */
        H.last_start = H.now;
        start((HapticId)H.pending, H.pending_k);
    }
    H.pending = HAPTIC_NONE;
}

void ui_haptic_set_level(HapticLevel level) { H.level = level; }
HapticLevel ui_haptic_level(void) { return H.level; }
bool ui_haptic_supported(void) { return H.forced >= 0 ? H.forced != 0 : haptic_hw_supported(); }

int ui_haptic_records(const HapticRecord **ring, uint64_t *total)
{
    *ring = H.ring;
    *total = H.total;
    return H.total < HAPTIC_RECORDS ? (int)H.total : HAPTIC_RECORDS;
}

void ui_haptic_force_supported(int forced) { H.forced = forced; }
void ui_haptic_simulate_reset(void) { haptic_hw_simulate_reset(); }
int ui_haptic_rebuilds(void) { return haptic_hw_rebuilds(); }
