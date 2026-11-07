#ifndef MG_LOG_H
#define MG_LOG_H

#if defined(__GNUC__) || defined(__clang__)
#define MG_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define MG_PRINTF(fmt_idx, arg_idx)
#endif

void mg_log_info(const char *fmt, ...) MG_PRINTF(1, 2);
void mg_log_warn(const char *fmt, ...) MG_PRINTF(1, 2);
void mg_log_error(const char *fmt, ...) MG_PRINTF(1, 2);

#endif
