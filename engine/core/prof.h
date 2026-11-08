/* CPU profiler: PROF_SCOPE("name") measures until the end of the C scope.
 * Hierarchical, at most PROF_MAX_SCOPES names, no allocation, main thread
 * only (other threads are ignored). With MG_PROF off the macros are empty.
 * Also the frame-interval histogram and the heap counter (everything that
 * goes through SDL_malloc on the main thread inside the frame loop; the OS
 * and Metal driver allocate behind SDL's back and are not seen). */
#ifndef MG_PROF_H
#define MG_PROF_H

#include "mg_types.h"

#define PROF_MAX_SCOPES 64
#define PROF_HISTORY 120

typedef struct ProfRow {
    const char *name;
    int depth;
    float avg_ms, p95_ms, max_ms; /* per frame */
} ProfRow;

typedef struct ProfFrames {
    uint32_t frames;
    uint32_t on_time;  /* <= 25 ms: 16.6 */
    uint32_t one_late; /* <= 42 ms: 33 */
    uint32_t worse;    /* 50+ */
} ProfFrames;

#ifdef MG_PROF

typedef struct ProfToken {
    int id;
    uint64_t t0;
} ProfToken;

ProfToken prof_begin_(const char *name, int *cache);
void prof_end_(ProfToken *t);

#define PROF_CAT2(a, b) a##b
#define PROF_CAT(a, b) PROF_CAT2(a, b)
#define PROF_SCOPE(name)                                                                                                       \
    static int PROF_CAT(prof_id_, __LINE__) = -1;                                                                              \
    __attribute__((cleanup(prof_end_))) ProfToken PROF_CAT(prof_tok_, __LINE__) = prof_begin_(name, &PROF_CAT(prof_id_, __LINE__))

void prof_init(void); /* first thing at startup: installs the heap counter */
void prof_set_enabled(bool on);
bool prof_enabled(void);
void prof_frame_begin(void);
void prof_frame_end(double frame_interval_s);
/* Top scopes by average over the last PROF_HISTORY frames. */
int prof_top(ProfRow *out, int max);
/* Since prof_run_reset: every scope in first-seen order (p95 at 0.05 ms resolution). */
void prof_run_reset(void);
int prof_run_rows(ProfRow *out, int max);
ProfFrames prof_run_frames(void);
uint32_t prof_allocs_last_frame(void);
uint32_t prof_allocs_run(void);          /* inside the frame loop since prof_run_reset */
uint32_t prof_alloc_frames_run(void);    /* frames with at least one */

#else

#define PROF_SCOPE(name) ((void)0)
static inline void prof_init(void) {}
static inline void prof_set_enabled(bool on) { (void)on; }
static inline bool prof_enabled(void) { return false; }
static inline void prof_frame_begin(void) {}
static inline void prof_frame_end(double frame_interval_s) { (void)frame_interval_s; }
static inline int prof_top(ProfRow *out, int max) { (void)out; (void)max; return 0; }
static inline void prof_run_reset(void) {}
static inline int prof_run_rows(ProfRow *out, int max) { (void)out; (void)max; return 0; }
static inline ProfFrames prof_run_frames(void) { ProfFrames f = {0, 0, 0, 0}; return f; }
static inline uint32_t prof_allocs_last_frame(void) { return 0; }
static inline uint32_t prof_allocs_run(void) { return 0; }
static inline uint32_t prof_alloc_frames_run(void) { return 0; }

#endif
#endif
