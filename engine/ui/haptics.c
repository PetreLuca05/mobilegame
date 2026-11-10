#include "haptics.h"

#include "platform/haptics.h"

#define WINDOW_S 0.05 /* at most one start per 50 ms */

/* The haptic engine is one per device, like the audio device: this module keeps its state. */
static struct {
    HapticLevel level;
    int forced;          /* -1 real capability */
    bool active;
    uint64_t frame;
    double now;
    double last_start;
    float last_strength; /* of the start that opened the window */
    HapticRecord ring[HAPTIC_RECORDS];
    uint64_t total;
} H = {HAPTIC_LEVEL_FULL, -1, true, 0, 0.0, -1e9, 0.0f, {{0}}, 0};

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
    const float level = H.level == HAPTIC_LEVEL_FINE ? 0.6f : 1.0f;
    int at = 0;
    const float strength = peak(e, &at) * k * level;
    /* one start per 50 ms; inside the window only something stronger than what opened it */
    if (H.now - H.last_start < WINDOW_S && strength <= H.last_strength) {
        return;
    }
    if (H.now - H.last_start >= WINDOW_S) {
        H.last_start = H.now;
    }
    H.last_strength = strength;
    H.ring[H.total % HAPTIC_RECORDS] = (HapticRecord){(int16_t)id, strength, e->ev[at].sharpness, H.frame};
    H.total++;
    if (ui_haptic_supported()) {
        haptic_hw_play((int)id, k * level);
    }
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
