/* Small OS queries for the performance HUD and --autotest perf. */
#ifndef MG_DEVICE_STATS_H
#define MG_DEVICE_STATS_H

/* 0 nominal, 1 fair, 2 serious, 3 critical; -1 where the OS has no such thing. */
int device_thermal_state(void);
/* The footprint the OS counts against the app, in MB; 0 if unknown. */
float device_resident_mb(void);

#endif
