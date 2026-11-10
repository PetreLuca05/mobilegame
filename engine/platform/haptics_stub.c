#include "haptics.h"

/* No Taptic Engine: silent, no log. */
static int g_rebuilds;

bool haptic_hw_supported(void) { return false; }
void haptic_hw_init(const struct HapticEntry *table, int count)
{
    (void)table, (void)count;
    g_rebuilds = 1;
}
void haptic_hw_play(int index, float scale) { (void)index, (void)scale; }
void haptic_hw_active(bool active) { (void)active; }
void haptic_hw_simulate_reset(void) { g_rebuilds++; }
int haptic_hw_rebuilds(void) { return g_rebuilds; }
