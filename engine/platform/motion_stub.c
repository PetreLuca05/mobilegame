#include "motion.h"

bool motion_available(void) { return false; }
void motion_start(void) {}
void motion_stop(void) {}
bool motion_gravity(float *x, float *y, float *z)
{
    (void)x, (void)y, (void)z;
    return false;
}
bool motion_rotation_rate(float *dps)
{
    (void)dps;
    return false;
}
bool motion_low_power(void) { return false; }
