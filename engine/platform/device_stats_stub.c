#include "device_stats.h"

int device_thermal_state(void) { return -1; }
float device_resident_mb(void) { return 0.0f; }
int device_boost_render_thread(void) { return 0; }
float device_screen_max_hz(void *native_window) { (void)native_window; return 0.0f; }
