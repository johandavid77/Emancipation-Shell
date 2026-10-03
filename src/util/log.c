#include "util/log.h"
#include <stdarg.h>
#include <time.h>

static enum log_level g_level = LOG_INFO;

void log_set_level(enum log_level level)
{
    g_level = level;
}

void log_msg(enum log_level level, const char *fmt, ...)
{
    if (level < g_level) {
        return;
    }
    const char *prefix = "";
    switch (level) {
    case LOG_DEBUG:
        prefix = "DEBUG";
        break;
    case LOG_INFO:
        prefix = "INFO";
        break;
    case LOG_WARN:
        prefix = "WARN";
        break;
    case LOG_ERR:
        prefix = "ERR";
        break;
    default:
        prefix = "LOG";
        break;
    }
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char ts[32];
    if (tm) {
        strftime(ts, sizeof(ts), "%H:%M:%S", tm);
    } else {
        ts[0] = '\0';
    }
    fprintf(stderr, "[%s] %s: ", ts, prefix);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}
