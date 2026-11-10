/* Taps on the Taptic Engine (iOS); no-op elsewhere. */
#ifndef MG_HAPTICS_H
#define MG_HAPTICS_H

#include <stdbool.h>

typedef enum HapticKind { HAPTIC_LIGHT, HAPTIC_MEDIUM, HAPTIC_HEAVY, HAPTIC_SUCCESS, HAPTIC_ERROR } HapticKind;

void haptic_impact(bool medium);
/* UI-1 tiers: impacts and the success / error notifications. */
void haptic_play(HapticKind kind);

#endif
