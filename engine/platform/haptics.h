/* The haptic hardware behind engine/ui/haptics.c (HAPTIC-1). iOS: Core Haptics, one engine and one
 * cached player per table entry (platform/ios/haptics.m); elsewhere a silent no-op. Only
 * engine/ui/haptics.c calls these. */
#ifndef MG_HAPTICS_H
#define MG_HAPTICS_H

#include <stdbool.h>

struct HapticEntry;

bool haptic_hw_supported(void);
/* Build every pattern and player, off the calling thread; plays before it is done are dropped. */
void haptic_hw_init(const struct HapticEntry *table, int count);
/* Start entry `index` now with its intensities x scale; never blocks, creates nothing. */
void haptic_hw_play(int index, float scale);
void haptic_hw_active(bool active);
void haptic_hw_simulate_reset(void); /* what the engine's reset handler does */
int haptic_hw_rebuilds(void);        /* players built so far (1 after init, +1 per reset) */

#endif
