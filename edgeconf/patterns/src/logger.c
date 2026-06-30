/* src/logger.c */
#include "logger.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>

static int s_level     = LOG_INFO;
static int s_to_stderr = 1;

void logger_init(const char *ident, int to_stderr, int level) {
    s_level     = level;
    s_to_stderr = to_stderr;
    if (!to_stderr)
        openlog(ident, LOG_PID | LOG_NDELAY, LOG_DAEMON);
}

void logger_close(void) {
    if (!s_to_stderr)
        closelog();
}

static const char *level_label(int level) {
    switch (level) {
    case LOG_ERR:     return "ERR";
    case LOG_WARNING: return "WARN";
    case LOG_INFO:    return "INFO";
    case LOG_DEBUG:   return "DEBUG";
    default:          return "????";
    }
}

void logger_write(int level, const char *fmt, ...) {
    /* Suppress messages below the configured threshold. */
    if (level > s_level)
        return;

    va_list ap;
    va_start(ap, fmt);

    if (s_to_stderr) {
        fprintf(stderr, "[%s] ", level_label(level));
        vfprintf(stderr, fmt, ap);
        fputc('\n', stderr);
    } else {
        vsyslog(level, fmt, ap);
    }

    va_end(ap);
}
