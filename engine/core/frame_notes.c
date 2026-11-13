#include "frame_notes.h"

#include <SDL3/SDL_stdinc.h>

#include "log.h"

#define NOTES_MAX 8

static struct {
    const char *what[NOTES_MAX];
    int count[NOTES_MAX];
    int n;
    FrameLate late[FRAME_LATE_KEEP];
    int late_n; /* total seen; the ring keeps the last FRAME_LATE_KEEP */
    FrameLate drop[FRAME_LATE_KEEP];
    int drop_n;
    double drop_ms;
} N;

void frame_notes_set_hz(double hz) { N.drop_ms = hz > 1.0 ? FRAME_DROP_FACTOR * 1000.0 / hz : 0.0; }

static void keep(FrameLate *l, double now_s, double dt_ms, const char *screen, const char *scene)
{
    l->at_s = now_s, l->ms = dt_ms;
    SDL_snprintf(l->screen, sizeof(l->screen), "%s", screen ? screen : "");
    SDL_snprintf(l->scene, sizeof(l->scene), "%s", scene ? scene : "");
    size_t o = 0;
    l->notes[0] = 0;
    for (int i = 0; i < N.n && o < sizeof(l->notes); i++) {
        const int w = SDL_snprintf(l->notes + o, sizeof(l->notes) - o, N.count[i] > 1 ? "%s%s x%d" : "%s%s", o ? ", " : "", N.what[i], N.count[i]);
        o += w > 0 ? (size_t)w : 0;
    }
}

void frame_note(const char *what)
{
    for (int i = 0; i < N.n; i++) {
        if (N.what[i] == what) {
            N.count[i]++;
            return;
        }
    }
    if (N.n < NOTES_MAX) {
        N.what[N.n] = what;
        N.count[N.n++] = 1;
    }
}

void frame_notes_check(double now_s, double dt_ms, const char *screen, const char *scene)
{
    const double drop_ms = N.drop_ms > 0.0 ? N.drop_ms : FRAME_DROP_FACTOR * 1000.0 / 60.0;
    if (dt_ms > drop_ms) {
        FrameLate *d = &N.drop[N.drop_n % FRAME_LATE_KEEP];
        N.drop_n++;
        keep(d, now_s, dt_ms, screen, scene);
        if (dt_ms <= FRAME_LATE_MS) {
            mg_log_info("dropped frame: %.1f ms at %.2f s, screen %s, scene %s, during it: %s", dt_ms, now_s, d->screen, d->scene,
                        d->notes[0] ? d->notes : "nothing noted");
        }
    }
    if (dt_ms > FRAME_LATE_MS) {
        FrameLate *l = &N.late[N.late_n % FRAME_LATE_KEEP];
        N.late_n++;
        keep(l, now_s, dt_ms, screen, scene);
        mg_log_warn("late frame: %.1f ms at %.2f s, screen %s, scene %s, during it: %s", dt_ms, now_s, l->screen, l->scene,
                    l->notes[0] ? l->notes : "nothing noted");
    }
    N.n = 0;
}

int frame_late_count(void) { return N.late_n < FRAME_LATE_KEEP ? N.late_n : FRAME_LATE_KEEP; }

const FrameLate *frame_late_get(int i)
{
    const int n = frame_late_count();
    if (i < 0 || i >= n) {
        return NULL;
    }
    const int first = N.late_n - n;
    return &N.late[(first + i) % FRAME_LATE_KEEP];
}

int frame_drop_count(void) { return N.drop_n < FRAME_LATE_KEEP ? N.drop_n : FRAME_LATE_KEEP; }

const FrameLate *frame_drop_get(int i)
{
    const int n = frame_drop_count();
    if (i < 0 || i >= n) {
        return NULL;
    }
    const int first = N.drop_n - n;
    return &N.drop[(first + i) % FRAME_LATE_KEEP];
}
