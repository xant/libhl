#ifndef __LOG_H__
#define __LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

void log_init(char *ident, int loglevel);
unsigned int log_level();
void log_message(int prio, int dbglevel, const char *fmt, ...);

#define ERROR(__fmt, __args...)      do { log_message(LOG_ERR,     0, __fmt, ## __args); } while (0)
#define WARNING(__fmt, __args...)    do { log_message(LOG_WARNING, 0, __fmt, ## __args); } while (0)
#define WARN(__fmt, __args...) WARNING(__fmt, ## __args)
#define NOTICE(__fmt, __args...)     do { log_message(LOG_NOTICE,  0, __fmt, ## __args); } while (0)
#define INFO(__fmt, __args...)       do { log_message(LOG_INFO,    0, __fmt, ## __args); } while (0)
#define DIE(__fmt, __args...)        do { ERROR(__fmt, ## __args); exit(-1); } while (0)

#define __DEBUG(__n, __fmt, __args...)  do { if (log_level() >= __n)  log_message(LOG_DEBUG,   __n, __fmt, ## __args); } while (0)

#define DEBUG(__fmt, __args...)  __DEBUG(1, __fmt, ## __args)
#define DEBUG2(__fmt, __args...) __DEBUG(2, __fmt, ## __args)
#define DEBUG3(__fmt, __args...) __DEBUG(3, __fmt, ## __args)
#define DEBUG4(__fmt, __args...) __DEBUG(4, __fmt, ## __args)
#define DEBUG5(__fmt, __args...) __DEBUG(5, __fmt, ## __args)

#ifdef __cplusplus
}
#endif

#endif

