/* --autotest haptics (HAPTIC-1), desktop or a device without a Taptic Engine: the recorder stands in
 * for the hardware (capability forced on so the Vibrații row shows).
 *   1 coverage: every entry has events, every widget kind a row; the test screen's widgets record their
 *     down and action entries; on the menu, Locker, Settings, Friends and map every pressable with a
 *     touch-down entry records it (pressed and dragged off, so nothing fires)
 *   2 ordering: link < secondary < primary < main on release; touch down below release; R5 only for
 *     destructive confirms and celebrations
 *   3 same frame: every recorded haptic is on the frame its input was processed
 *   4 limiter: 20 presses in 200 ms -> <= 4 starts; a slider end to end in 0.5 s -> <= 10 ticks + 1 end bump
 *   5 setting: Oprit records nothing; Fin 0.6x and no R1; the row is absent without the capability
 *   6 sequences: a reward of 8 tokens records 8 rising taps ending at R4; a wrong answer ERROR, no SUCCESS
 *   7 lifecycle: background -> foreground, a press still records; a simulated engine reset rebuilds */
#ifndef MG_AUTOTEST_HAPTICS_H
#define MG_AUTOTEST_HAPTICS_H

#include <SDL3/SDL.h>

#include "platform/app.h"

struct AutotestProbe;
struct AutotestOut;

typedef struct HapticTest {
    int stage, sub, item, screen_i, pressed, recorded, skipped;
    double at;
    uint64_t mark, down_frame;
    float x, y;
    int expect;
    char why[200];
} HapticTest;

int ht_run(HapticTest *t, MgApp *app, const struct AutotestProbe *p, struct AutotestOut *out, double now);

#endif
