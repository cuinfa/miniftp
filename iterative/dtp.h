#pragma once

#include "session.h"
#include <sys/types.h>  // ssize_t

#define PWDFILE "/etc/ausftp/ftpusers"

int check_credentials(char *user, char *pass);

// Active mode: parse PORT args and store in sess->data_addr
int dtp_parse_port(const char *args, ftp_session_t *sess);
 
// Active mode: connect to client data address (sess->data_addr)
int dtp_connect_active(ftp_session_t *sess);
