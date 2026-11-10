// TILT-1: Core Motion device motion behind engine/platform/motion.h. The handler runs on its own queue at
// 60 Hz and only stores gravity in atomics; the main tick reads them (no object, no allocation).
#import <CoreMotion/CoreMotion.h>
#import <Foundation/Foundation.h>

#include <stdatomic.h>
#include <string.h>

#include "platform/motion.h"

static CMMotionManager *g_mm;
static NSOperationQueue *g_queue;
static atomic_uint g_gx, g_gy, g_gz;
static atomic_bool g_have;

static unsigned bits(double v)
{
    const float f = (float)v;
    unsigned u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

static float flt(unsigned u)
{
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

bool motion_available(void)
{
    if (!g_mm) {
        g_mm = [[CMMotionManager alloc] init]; /* one per app */
    }
    return g_mm.deviceMotionAvailable;
}

void motion_start(void)
{
    if (!motion_available() || g_mm.deviceMotionActive) {
        return;
    }
    if (!g_queue) {
        g_queue = [[NSOperationQueue alloc] init];
        g_queue.maxConcurrentOperationCount = 1;
    }
    g_mm.deviceMotionUpdateInterval = 1.0 / 60.0;
    [g_mm startDeviceMotionUpdatesToQueue:g_queue
                              withHandler:^(CMDeviceMotion *m, NSError *e) {
                                  if (!m || e) {
                                      return;
                                  }
                                  atomic_store(&g_gx, bits(m.gravity.x));
                                  atomic_store(&g_gy, bits(m.gravity.y));
                                  atomic_store(&g_gz, bits(m.gravity.z));
                                  atomic_store(&g_have, true);
                              }];
}

void motion_stop(void)
{
    if (g_mm && g_mm.deviceMotionActive) {
        [g_mm stopDeviceMotionUpdates];
    }
    atomic_store(&g_have, false);
}

bool motion_gravity(float *x, float *y, float *z)
{
    if (!atomic_load(&g_have)) {
        return false;
    }
    *x = flt(atomic_load(&g_gx)), *y = flt(atomic_load(&g_gy)), *z = flt(atomic_load(&g_gz));
    return true;
}

bool motion_low_power(void) { return NSProcessInfo.processInfo.lowPowerModeEnabled; }
