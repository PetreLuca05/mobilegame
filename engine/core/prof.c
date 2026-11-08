#include "prof.h"

#ifdef MG_PROF

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

#define STACK_DEPTH 16
#define RUN_BUCKETS 256
#define RUN_BUCKET_MS 0.05f

typedef struct Scope {
    const char *name;
    int depth;
    uint64_t ticks; /* this frame */
    float history[PROF_HISTORY];
    double run_sum;
    float run_max;
    uint32_t run_hist[RUN_BUCKETS];
} Scope;

/* Like the logger, the profiler has to be reachable from any function. */
static struct {
    bool enabled, want_enabled, in_frame;
    SDL_ThreadID main_thread;
    double ticks_to_ms;
    Scope scopes[PROF_MAX_SCOPES];
    int count;
    int stack[STACK_DEPTH];
    int sp;
    uint32_t frame;      /* frames finished */
    uint32_t run_frames;
    ProfFrames intervals;
    uint32_t allocs, allocs_last, allocs_run, alloc_frames, exempt_run;
    int exempt;
    SDL_malloc_func real_malloc;
    SDL_calloc_func real_calloc;
    SDL_realloc_func real_realloc;
    SDL_free_func real_free;
} P;

static void count_alloc(void)
{
    if (P.in_frame && SDL_GetCurrentThreadID() == P.main_thread) {
        if (P.exempt > 0) {
            P.exempt_run++;
        } else {
            P.allocs++;
        }
    }
}

static void *SDLCALL hook_malloc(size_t n)
{
    count_alloc();
    return P.real_malloc(n);
}

static void *SDLCALL hook_calloc(size_t a, size_t b)
{
    count_alloc();
    return P.real_calloc(a, b);
}

static void *SDLCALL hook_realloc(void *p, size_t n)
{
    count_alloc();
    return P.real_realloc(p, n);
}

static void SDLCALL hook_free(void *p) { P.real_free(p); }

void prof_init(void)
{
    P.main_thread = SDL_GetCurrentThreadID();
    P.ticks_to_ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
    /* the hooks forward to the same allocator, so blocks from before this call stay valid */
    SDL_GetOriginalMemoryFunctions(&P.real_malloc, &P.real_calloc, &P.real_realloc, &P.real_free);
    SDL_SetMemoryFunctions(hook_malloc, hook_calloc, hook_realloc, hook_free);
}

/* takes effect on the next frame, so the scope stack starts balanced */
void prof_set_enabled(bool on) { P.want_enabled = on; }
bool prof_enabled(void) { return P.want_enabled; }

static int scope_id(const char *name)
{
    for (int i = 0; i < P.count; i++) {
        if (SDL_strcmp(P.scopes[i].name, name) == 0) {
            return i;
        }
    }
    if (P.count >= PROF_MAX_SCOPES) {
        return -1;
    }
    Scope *s = &P.scopes[P.count];
    s->name = name;
    s->depth = P.sp;
    return P.count++;
}

ProfToken prof_begin_(const char *name, int *cache)
{
    ProfToken t = {-1, 0};
    if (!P.enabled || !P.in_frame || P.sp >= STACK_DEPTH || SDL_GetCurrentThreadID() != P.main_thread) {
        return t;
    }
    if (*cache < 0) {
        *cache = scope_id(name);
        if (*cache < 0) {
            return t;
        }
    }
    t.id = *cache;
    P.stack[P.sp++] = t.id;
    t.t0 = SDL_GetPerformanceCounter();
    return t;
}

void prof_end_(ProfToken *t)
{
    if (t->id >= 0) {
        P.scopes[t->id].ticks += SDL_GetPerformanceCounter() - t->t0;
        P.sp--;
    }
}

void prof_frame_begin(void)
{
    P.in_frame = true;
    P.enabled = P.want_enabled;
    P.allocs = 0;
}

void prof_frame_end(double frame_interval_s)
{
    P.in_frame = false;
    P.allocs_last = P.allocs;
    P.allocs_run += P.allocs;
    P.alloc_frames += P.allocs > 0;
    if (!P.enabled) {
        return;
    }
    const uint32_t slot = P.frame % PROF_HISTORY;
    for (int i = 0; i < P.count; i++) {
        Scope *s = &P.scopes[i];
        float ms = (float)((double)s->ticks * P.ticks_to_ms);
        s->ticks = 0;
        s->history[slot] = ms;
        s->run_sum += (double)ms;
        s->run_max = ms > s->run_max ? ms : s->run_max;
        int b = (int)(ms / RUN_BUCKET_MS);
        s->run_hist[b >= RUN_BUCKETS ? RUN_BUCKETS - 1 : b]++;
    }
    P.frame++;
    P.run_frames++;
    if (frame_interval_s > 0.0) {
        P.intervals.frames++;
        if (frame_interval_s <= 0.025) {
            P.intervals.on_time++;
        } else if (frame_interval_s <= 0.042) {
            P.intervals.one_late++;
        } else {
            P.intervals.worse++;
        }
    }
}

static void history_row(const Scope *s, int frames, ProfRow *r)
{
    float v[PROF_HISTORY];
    float sum = 0.0f, mx = 0.0f;
    for (int i = 0; i < frames; i++) { /* insertion sort: 120 values, a few times a second */
        float x = s->history[i];
        sum += x;
        mx = x > mx ? x : mx;
        int j = i;
        while (j > 0 && v[j - 1] > x) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = x;
    }
    r->name = s->name;
    r->depth = s->depth;
    r->avg_ms = frames ? sum / (float)frames : 0.0f;
    r->p95_ms = frames ? v[(frames * 95) / 100] : 0.0f;
    r->max_ms = mx;
}

int prof_top(ProfRow *out, int max)
{
    const int frames = P.frame < PROF_HISTORY ? (int)P.frame : PROF_HISTORY;
    int n = 0;
    for (int i = 0; i < P.count; i++) {
        ProfRow r;
        history_row(&P.scopes[i], frames, &r);
        if (n == max && r.avg_ms <= out[n - 1].avg_ms) {
            continue;
        }
        int j = n < max ? n++ : max - 1;
        while (j > 0 && out[j - 1].avg_ms < r.avg_ms) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = r;
    }
    return n;
}

void prof_run_reset(void)
{
    for (int i = 0; i < P.count; i++) {
        Scope *s = &P.scopes[i];
        s->run_sum = 0.0;
        s->run_max = 0.0f;
        SDL_memset(s->run_hist, 0, sizeof(s->run_hist));
    }
    P.run_frames = 0;
    SDL_memset(&P.intervals, 0, sizeof(P.intervals));
    P.allocs_run = P.alloc_frames = P.exempt_run = 0;
}

int prof_run_rows(ProfRow *out, int max)
{
    int n = 0;
    for (int i = 0; i < P.count && n < max; i++) {
        const Scope *s = &P.scopes[i];
        ProfRow *r = &out[n++];
        r->name = s->name;
        r->depth = s->depth;
        r->avg_ms = P.run_frames ? (float)(s->run_sum / (double)P.run_frames) : 0.0f;
        r->max_ms = s->run_max;
        uint32_t want = (P.run_frames * 95) / 100, seen = 0;
        int b = 0;
        while (b < RUN_BUCKETS - 1 && seen + s->run_hist[b] <= want) {
            seen += s->run_hist[b++];
        }
        r->p95_ms = (float)(b + 1) * RUN_BUCKET_MS;
    }
    return n;
}

ProfFrames prof_run_frames(void) { return P.intervals; }
uint32_t prof_allocs_last_frame(void) { return P.allocs_last; }
uint32_t prof_allocs_run(void) { return P.allocs_run; }
uint32_t prof_alloc_frames_run(void) { return P.alloc_frames; }
void prof_allocs_exempt(bool on) { P.exempt += on ? 1 : -1; }
uint32_t prof_allocs_exempt_run(void) { return P.exempt_run; }

#endif
