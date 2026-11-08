#import <UIKit/UIKit.h>

#include <stdbool.h>

#include "platform/haptics.h"

void haptic_impact(bool medium)
{
    UIImpactFeedbackGenerator *g = [[UIImpactFeedbackGenerator alloc]
        initWithStyle:medium ? UIImpactFeedbackStyleMedium : UIImpactFeedbackStyleLight];
    [g impactOccurred];
}
