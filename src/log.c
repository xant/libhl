#include "log.h"

static unsigned int __loglevel = 0;
void log_init(char *ident, int loglevel)
{
    __loglevel = loglevel;
    openlog(ident, LOG_CONS|LOG_PERROR, LOG_LOCAL0);
    setlogmask(LOG_UPTO(LOG_DEBUG));
}

unsigned int log_level()
{
    return __loglevel;
}

void log_message(int prio, int dbglevel, const char *fmt, ...)
{
    char *newfmt = NULL;
    const char *prefix = NULL;

    switch (prio) {
        case LOG_ERR:
            prefix = "[ERROR]: ";
            break;
        case LOG_WARNING:
            prefix = "[WARNING]: ";
            break;
        case LOG_NOTICE:
            prefix = "[NOTICE]: ";
            break;
        case LOG_INFO:
            prefix = "[INFO]: ";
            break;
        case LOG_DEBUG:
            switch (dbglevel) {
                case 1:
                    prefix = "[DBG]: ";
                    break;
                case 2:
                    prefix = "[DBG2]: ";
                    break;
                case 3:
                    prefix = "[DBG3]: ";
                    break;
                case 4:
                    prefix = "[DBG4]: ";
                    break;
                default:
                    prefix = "[DBGX]: ";
                    break;
            }
            break;
        default:
            prefix = "[UNKNOWN]: ";
            break;
    }

    // ensure the user passed a valid 'fmt' pointer before proceeding
    if (prefix && fmt) { 
        newfmt = (char *)calloc(1, strlen(fmt)+strlen(prefix)+1);
        if (newfmt) { // safety belts in case we are out of memory
            sprintf(newfmt, "%s%s", prefix, fmt);
            va_list arg;
            va_start(arg, fmt);
            vsyslog(prio, newfmt, arg);
            va_end(arg);
            free(newfmt);
        }
    }
}
