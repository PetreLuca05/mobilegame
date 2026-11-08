#include "jobs.h"

#include <SDL3/SDL.h>

/* state = generation << 20 | next item: a worker claims a batch with one
 * compare-and-swap, so a late one cannot run the new job with the old function. */
#define CURSOR_BITS 20
#define CURSOR_MASK ((1 << CURSOR_BITS) - 1)
#define GEN_MASK ((int)(0x7FFFFFFFu >> CURSOR_BITS))

struct JobSystem {
    SDL_Thread *threads[JOBS_MAX_THREADS];
    int worker_count; /* without the caller */
    int active;       /* threads in use, with the caller */
    SDL_Semaphore *wake;
    SDL_AtomicInt state, pending, quit;
    JobFn fn;
    void *ctx;
    int count, batch;
    Arena scratch[JOBS_MAX_THREADS];
};

typedef struct WorkerArg {
    JobSystem *js;
    int index;
} WorkerArg;

/* Runs batches until none is left. */
static void drain(JobSystem *js, int worker)
{
    for (;;) {
        int s = SDL_GetAtomicInt(&js->state);
        int first = s & CURSOR_MASK;
        JobFn fn = js->fn;
        void *ctx = js->ctx;
        int count = js->count, batch = js->batch;
        if (first >= count || !fn) {
            return;
        }
        int last = first + batch > count ? count : first + batch;
        if (!SDL_CompareAndSwapAtomicInt(&js->state, s, (s & ~CURSOR_MASK) | last)) {
            continue;
        }
        fn(ctx, first, last, worker);
        SDL_AddAtomicInt(&js->pending, -(last - first));
    }
}

static int SDLCALL worker_main(void *arg)
{
    WorkerArg a = *(WorkerArg *)arg;
    SDL_free(arg);
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_NORMAL);
    for (;;) {
        SDL_WaitSemaphore(a.js->wake);
        if (SDL_GetAtomicInt(&a.js->quit)) {
            return 0;
        }
        drain(a.js, a.index);
    }
}

JobSystem *jobs_create(Arena *perm, int workers, size_t scratch_per_thread)
{
    JobSystem *js = ARENA_PUSH(perm, JobSystem, 1);
    if (!js) {
        return NULL;
    }
    SDL_zerop(js);
    if (workers < 0) {
        workers = SDL_GetNumLogicalCPUCores() - 1;
    }
    workers = workers > JOBS_MAX_THREADS - 1 ? JOBS_MAX_THREADS - 1 : (workers < 0 ? 0 : workers);
    for (int i = 0; i <= workers; i++) {
        if (!arena_sub(perm, &js->scratch[i], scratch_per_thread)) {
            return NULL;
        }
    }
    js->wake = SDL_CreateSemaphore(0);
    for (int i = 0; i < workers && js->wake; i++) {
        WorkerArg *a = (WorkerArg *)SDL_malloc(sizeof(WorkerArg));
        a->js = js;
        a->index = i + 1;
        js->threads[i] = SDL_CreateThread(worker_main, "job", a);
        if (!js->threads[i]) {
            SDL_free(a);
            break;
        }
        js->worker_count++;
    }
    js->active = js->worker_count + 1;
    return js;
}

void jobs_destroy(JobSystem *js)
{
    if (!js) {
        return;
    }
    SDL_SetAtomicInt(&js->quit, 1);
    for (int i = 0; i < js->worker_count; i++) {
        SDL_SignalSemaphore(js->wake);
    }
    for (int i = 0; i < js->worker_count; i++) {
        SDL_WaitThread(js->threads[i], NULL);
    }
    SDL_DestroySemaphore(js->wake);
}

int jobs_thread_count(const JobSystem *js) { return js->active; }

void jobs_limit_threads(JobSystem *js, int threads)
{
    jobs_wait(js);
    js->active = threads < 1 ? 1 : (threads > js->worker_count + 1 ? js->worker_count + 1 : threads);
}

void jobs_frame_begin(JobSystem *js)
{
    for (int i = 0; i <= js->worker_count; i++) {
        arena_reset(&js->scratch[i]);
    }
}

Arena *jobs_scratch(JobSystem *js, int worker) { return &js->scratch[worker]; }

void jobs_run(JobSystem *js, JobFn fn, void *ctx, int count, int batch)
{
    jobs_wait(js);
    if (count <= 0) {
        return;
    }
    if (js->active <= 1) { /* no threads: on the caller, in order */
        fn(ctx, 0, count, 0);
        return;
    }
    SDL_SetAtomicInt(&js->pending, count);
    /* exhausted cursor first: nobody can claim while the fields change */
    int gen = ((SDL_GetAtomicInt(&js->state) >> CURSOR_BITS) + 1) & GEN_MASK;
    SDL_SetAtomicInt(&js->state, (gen << CURSOR_BITS) | CURSOR_MASK);
    js->fn = fn;
    js->ctx = ctx;
    js->count = count;
    js->batch = batch < 1 ? 1 : batch;
    SDL_MemoryBarrierRelease();
    SDL_SetAtomicInt(&js->state, gen << CURSOR_BITS);
    for (int i = 0; i < js->active - 1; i++) {
        SDL_SignalSemaphore(js->wake);
    }
}

void jobs_wait(JobSystem *js)
{
    if (js->active <= 1) {
        return;
    }
    drain(js, 0);
    while (SDL_GetAtomicInt(&js->pending) > 0) {
        SDL_CPUPauseInstruction();
    }
}
