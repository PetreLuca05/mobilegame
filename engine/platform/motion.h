/* TILT-1: the fused gravity vector (Core Motion device motion; SDL3's iOS sensors are only raw accelerometer
 * and gyro). Updates at 60 Hz on a background queue that only stores the latest vector; reading it never
 * allocates. No-op without a motion sensor (desktop). */
#ifndef MG_MOTION_H
#define MG_MOTION_H

#include <stdbool.h>

bool motion_available(void);
void motion_start(void); /* app active and the effect on */
void motion_stop(void);  /* background, or the effect off */
/* The latest gravity in device coordinates (g units, portrait x right, y up, z out of the screen); false if none yet. */
bool motion_gravity(float *x, float *y, float *z);
/* The rotation rate about the screen's axes (gyro, deg/s); false if none. */
bool motion_rotation_rate(float *dps);
bool motion_low_power(void); /* iOS Low Power Mode */

#endif
