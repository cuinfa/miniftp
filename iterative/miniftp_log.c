#include "miniftp_log.h"
#include <syslog.h>

void log_init(const char *ident, int use_syslog, const char *logfile) {
    (void)use_syslog;
    (void)logfile;
    openlog(ident, LOG_PID | LOG_CONS, LOG_DAEMON);
}

void log_close(void) {
    closelog();
}