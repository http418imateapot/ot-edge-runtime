/* src/logger.h
 * Structured logging abstraction: stderr during development, syslog in production.
 * Uses syslog priority levels (LOG_ERR, LOG_WARNING, LOG_INFO, LOG_DEBUG).
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <syslog.h>
#include <stdarg.h>

/* Initialise the logger.
 *   ident        - program name forwarded to openlog()
 *   to_stderr    - non-zero: write to stderr; zero: write to syslog (production)
 *   level        - maximum priority to emit, e.g. LOG_INFO silences LOG_DEBUG
 */
void logger_init(const char *ident, int to_stderr, int level);

/* Flush and close the logger (calls closelog when using syslog). */
void logger_close(void);

/* Low-level write; prefer the macros below. */
void logger_write(int level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Convenience macros mirroring syslog priority names */
#define log_err(fmt, ...)   logger_write(LOG_ERR,     fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...)  logger_write(LOG_WARNING,  fmt, ##__VA_ARGS__)
#define log_info(fmt, ...)  logger_write(LOG_INFO,     fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...) logger_write(LOG_DEBUG,    fmt, ##__VA_ARGS__)

#endif /* LOGGER_H */
