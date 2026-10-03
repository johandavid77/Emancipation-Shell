#ifndef EMANCIPATION_LOG_H
#define EMANCIPATION_LOG_H

#include <stdio.h>

enum log_level {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERR   = 3
};

void log_set_level(enum log_level level);
void log_msg(enum log_level level, const char *fmt, ...);

#define log_debug(...) log_msg(LOG_DEBUG, __VA_ARGS__)
#define log_info(...)  log_msg(LOG_INFO,  __VA_ARGS__)
#define log_warn(...)  log_msg(LOG_WARN,  __VA_ARGS__)
#define log_err(...)   log_msg(LOG_ERR,   __VA_ARGS__)

#endif /* EMANCIPATION_LOG_H */
