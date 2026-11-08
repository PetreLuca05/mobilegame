/* Small fixed thread pool: one parallel-for at a time. The caller helps in
 * jobs_wait. Workers never touch render state; they get a scratch arena that
 * jobs_frame_begin resets. */
#ifndef MG_JOBS_H
#define MG_JOBS_H

#include "arena.h"
#include "mg_types.h"

#define JOBS_MAX_THREADS 8 /* workers + the caller */

typedef struct JobSystem JobSystem;

/* Items [first, last); worker 0 is the caller. */
typedef void (*JobFn)(void *ctx, int first, int last, int worker);

/* workers < 0: cores - 1 */
JobSystem *jobs_create(Arena *perm, int workers, size_t scratch_per_thread);
void jobs_destroy(JobSystem *js);
int jobs_thread_count(const JobSystem *js);
/* Use only this many threads from now on (determinism tests); 1 = run inline. */
void jobs_limit_threads(JobSystem *js, int threads);
void jobs_frame_begin(JobSystem *js);
Arena *jobs_scratch(JobSystem *js, int worker);
/* Start a parallel-for in batches of `batch` items; a running one is finished first. */
void jobs_run(JobSystem *js, JobFn fn, void *ctx, int count, int batch);
/* Helps until the parallel-for has finished. */
void jobs_wait(JobSystem *js);

#endif
