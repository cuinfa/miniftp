#ifndef MINIFTP_LOG_H
#define MINIFTP_LOG_H

#include <syslog.h>

void log_init(const char *ident, int use_syslog, const char *logfile);
void log_close(void);

#define log_info(fmt, ...)  syslog(LOG_INFO,    "[INFO]  " fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...)  syslog(LOG_WARNING, "[WARN]  " fmt, ##__VA_ARGS__)
#define log_error(fmt, ...) syslog(LOG_ERR,     "[ERROR] " fmt, ##__VA_ARGS__)

#endif /* MINIFTP_LOG_H */