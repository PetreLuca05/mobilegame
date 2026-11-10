#import <UIKit/UIKit.h>

#include <stdbool.h>

#include "platform/haptics.h"

void haptic_impact(bool medium) { haptic_play(medium ? HAPTIC_MEDIUM : HAPTIC_LIGHT); }

void haptic_play(HapticKind kind)
{
    if (kind == HAPTIC_SUCCESS || kind == HAPTIC_ERROR) {
        UINotificationFeedbackGenerator *n = [[UINotificationFeedbackGenerator alloc] init];
        [n notificationOccurred:kind == HAPTIC_SUCCESS ? UINotificationFeedbackTypeSuccess : UINotificationFeedbackTypeError];
        return;
    }
    const UIImpactFeedbackStyle style = kind == HAPTIC_HEAVY ? UIImpactFeedbackStyleHeavy : kind == HAPTIC_MEDIUM ? UIImpactFeedbackStyleMedium : UIImpactFeedbackStyleLight;
    UIImpactFeedbackGenerator *g = [[UIImpactFeedbackGenerator alloc] initWithStyle:style];
    [g impactOccurred];
}
