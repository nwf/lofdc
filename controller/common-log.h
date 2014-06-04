#ifndef LOFDC_COMMON_LOG_H
#define LOFDC_COMMON_LOG_H

/* Logging utilities -- common-log.c */
struct evbuffer *log_eb;
void log_printf(const char *fmt, ...);

#endif
