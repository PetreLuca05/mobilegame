#include "log.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <stdarg.h>

/* SDL formats into a 256-byte stack buffer and SDL_mallocs anything longer, which the frame-loop allocation counter
 * sees (a long autotest or perf line would count as a heap allocation). Format here on the stack instead and hand the
 * line straight to the output function, after the same priority filter SDL applies. */
#define MG_LOG_MAX 1024

static void log_v(SDL_LogPriority priority, const char *fmt, va_list ap)
{
    if (priority < SDL_GetLogPriority(SDL_LOG_CATEGORY_APPLICATION)) {
        return;
    }
    char line[MG_LOG_MAX];
    const int n = SDL_vsnprintf(line, sizeof(line), fmt, ap);
    if (n < 0) {
        return;
    }
    if (n < 256) { /* fits SDL's own stack buffer */
        SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, priority, "%s", line);
        return;
    }
    SDL_LogOutputFunction out = NULL;
    void *user = NULL;
    SDL_GetLogOutputFunction(&out, &user);
    if (out) {
        out(user, SDL_LOG_CATEGORY_APPLICATION, priority, line); /* longer than MG_LOG_MAX: cut, never allocated */
    }
}

void mg_log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_v(SDL_LOG_PRIORITY_INFO, fmt, ap);
    va_end(ap);
}

void mg_log_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_v(SDL_LOG_PRIORITY_WARN, fmt, ap);
    va_end(ap);
}

void mg_log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_v(SDL_LOG_PRIORITY_ERROR, fmt, ap);
    va_end(ap);
}
