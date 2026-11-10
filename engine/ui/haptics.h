/* HAPTIC-1: the only way the game plays a haptic. ui_haptic(HAPTIC_*) -> the limiter (one start
 * per 50 ms; inside the window only something stronger than what started it gets through; a frame's
 * requests keep the strongest) -> the level (Oprit / Fin x0.6 without R1 / Complet) -> the recorder
 * (256 entries, always) and the hardware (iPhone Core Haptics; a silent no-op without a Taptic Engine).
 * Nothing outside this file and platform/ios/haptics.m touches haptic hardware. */
#ifndef MG_UI_HAPTICS_H
#define MG_UI_HAPTICS_H

#include <stdbool.h>
#include <stdint.h>

#include "haptic_table.h"

typedef enum HapticLevel { HAPTIC_LEVEL_OFF, HAPTIC_LEVEL_FINE, HAPTIC_LEVEL_FULL } HapticLevel;

typedef struct HapticRecord {
    int16_t id;
    float intensity, sharpness; /* the strongest event, after the level */
    uint64_t frame;
} HapticRecord;

#define HAPTIC_RECORDS 256

void ui_haptics_init(void);                     /* builds the players off the first frame (iOS) */
void ui_haptics_frame(uint64_t frame, double now); /* once per frame, before the UI */
void ui_haptics_active(bool active);            /* foreground / background */
void ui_haptic(HapticId id);
void ui_haptic_scaled(HapticId id, float k);   /* rising sequences: intensity x k */
void ui_haptic_set_level(HapticLevel level);
HapticLevel ui_haptic_level(void);
bool ui_haptic_supported(void);                 /* CHHapticEngine capabilities, never the model */
/* autotests and the lab */
int ui_haptic_records(const HapticRecord **ring, uint64_t *total); /* ring[total % 256] is the next slot */
void ui_haptic_force_supported(int forced);    /* -1 real, 0 / 1 forced (tests) */
void ui_haptic_simulate_reset(void);
int ui_haptic_rebuilds(void);

#endif
