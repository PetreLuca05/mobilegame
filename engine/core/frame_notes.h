/* Late frames (a tracked bug: rare Mac stalls, e.g. one 1.2 s frame in a menu run): expensive one-off work leaves a note
 * in a fixed per-frame buffer (frame_note), and a frame that took longer than FRAME_LATE_MS is logged at the start of the
 * next one with its time, the screen, the scene and those notes ("late frame: ..."). The last FRAME_LATE_KEEP stay in
 * memory for the perf JSON ("late_frames"), so a device run brings them along. No allocation. */
#ifndef MG_FRAME_NOTES_H
#define MG_FRAME_NOTES_H

#include <stdint.h>

#define FRAME_LATE_MS 50.0
#define FRAME_LATE_KEEP 32

typedef struct FrameLate {
    double at_s, ms;
    char screen[16], scene[16];
    char notes[96];
} FrameLate;

/* Something expensive happened this frame (a static string; repeats are counted, not stored twice). */
void frame_note(const char *what);
/* At the start of a frame: dt_ms is the frame that just ended. Logs and keeps it when late, then clears the notes. */
void frame_notes_check(double now_s, double dt_ms, const char *screen, const char *scene);
int frame_late_count(void);
const FrameLate *frame_late_get(int i);
/* Dropped frames: longer than FRAME_DROP_FACTOR refresh intervals (25 ms at 60 Hz; set the refresh with frame_notes_set_hz), kept
 * the same way with their notes (the perf JSON's "dropped_frames"); late ones are among them. */
#define FRAME_DROP_FACTOR 1.5
void frame_notes_set_hz(double hz);
int frame_drop_count(void);
const FrameLate *frame_drop_get(int i);

#endif
