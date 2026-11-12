/* Small OS queries for the performance HUD and --autotest perf. */
#ifndef MG_DEVICE_STATS_H
#define MG_DEVICE_STATS_H

/* 0 nominal, 1 fair, 2 serious, 3 critical; -1 where the OS has no such thing. */
int device_thermal_state(void);
/* The footprint the OS counts against the app, in MB; 0 if unknown. */
float device_resident_mb(void);
/* iOS: the calling (GPU-encoding) thread goes to fixed priority 45, round robin,
 * which also opts it out of the scheduler's priority decay. Returns the
 * priority actually in force, 0 where nothing was done. */
int device_boost_render_thread(void);
/* The highest refresh the window's screen can run at (ProMotion: 120 where SDL's display mode says 60), 0 if unknown.
 * `native_window`: SDL's NSWindow / UIWindow pointer. */
float device_screen_max_hz(void *native_window);
/* macOS: the main display is asleep or the session is locked (no screen refresh paces the window then); 0 elsewhere. */
int device_display_asleep(void);

#endif
