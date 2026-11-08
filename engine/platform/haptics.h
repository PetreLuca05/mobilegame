/* Light / medium taps on the Taptic Engine (iOS); no-op elsewhere. */
#ifndef MG_HAPTICS_H
#define MG_HAPTICS_H

#include <stdbool.h>

void haptic_impact(bool medium);

#endif
