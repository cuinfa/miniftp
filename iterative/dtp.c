#define _GNU_SOURCE
#include "dtp.h"
#include "miniftp_log.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
 
int check_credentials(char *user, char *pass) {
  FILE *file;
  char *path = PWDFILE, *line = NULL, cred[100];
  size_t len = 0;
  int found = -1;
 
  sprintf(cred, "%s:%s", user, pass);
 
  file = fopen(path, "r");
  if (file == NULL) {
    log_error("Could not open users file '%s': %s", path, strerror(errno));
    return -1;
  }
 
  while (getline(&line, &len, file) != -1) {
    strtok(line, "\n");
    if (strcmp(line, cred) == 0) {
      found = 0;
      break;
    }
  }
 
  fclose(file);
  if (line) free(line);
  return found;
}

// Active mode: parse "h1,h2,h3,h4,p1,p2" and store in sess->data_addr

int dtp_parse_port(const char *args, ftp_session_t *sess) {
  if (!args || !sess) {
    log_error("dtp_parse_port: NULL argument");
    return -1;
  }
 
  unsigned int h1, h2, h3, h4, p1, p2;
 
  if (sscanf(args, "%u,%u,%u,%u,%u,%u", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
    log_error("dtp_parse_port: invalid PORT arguments: '%s'", args);
    return -1;
  }
 
  if (h1 > 255 || h2 > 255 || h3 > 255 || h4 > 255 || p1 > 255 || p2 > 255) {
    log_error("dtp_parse_port: out-of-range values in PORT: '%s'", args);
    return -1;
  }
 
  char ip[INET_ADDRSTRLEN];
  snprintf(ip, sizeof(ip), "%u.%u.%u.%u", h1, h2, h3, h4);
  int port = (int)(p1 * 256 + p2);
 
  memset(&sess->data_addr, 0, sizeof(sess->data_addr));
  sess->data_addr.sin_family = AF_INET;
  sess->data_addr.sin_port   = htons((uint16_t)port);
 
  if (inet_pton(AF_INET, ip, &sess->data_addr.sin_addr) != 1) {
    log_error("dtp_parse_port: invalid IP '%s'", ip);
    return -1;
  }
 
  log_info("PORT parsed: client data address %s:%d", ip, port);
  return 0;
}

// Active mode: open TCP connection to the client's data address
int dtp_connect_active(ftp_session_t *sess) {
  if (!sess) {
    log_error("dtp_connect_active: NULL session");
    return -1;
  }
 
  if (sess->data_sock >= 0) {
    close(sess->data_sock);
    sess->data_sock = -1;
  }
 
  int data_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (data_fd < 0) {
    log_error("dtp_connect_active: socket: %s", strerror(errno));
    return -1;
  }
 
  // RFC 959: server MUST connect from port 20 (ftp-data)
  struct sockaddr_in local = {
    .sin_family      = AF_INET,
    .sin_port        = htons(20),
    .sin_addr.s_addr = INADDR_ANY,
  };
 
  const int opt = 1;
  if (setsockopt(data_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    log_warn("dtp_connect_active: SO_REUSEADDR: %s", strerror(errno));
 
  if (bind(data_fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
    log_warn("dtp_connect_active: bind port 20 failed (%s), using ephemeral port",
             strerror(errno));
    // Not fatal when running without root, fall through with ephemeral port
    local.sin_port = 0;
    bind(data_fd, (struct sockaddr *)&local, sizeof(local));
  }
 
  if (connect(data_fd, (struct sockaddr *)&sess->data_addr,
              sizeof(sess->data_addr)) < 0) {
    log_error("dtp_connect_active: connect: %s", strerror(errno));
    close(data_fd);
    return -1;
  }
 
  char ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &sess->data_addr.sin_addr, ip, sizeof(ip));
  log_info("Data connection established to %s:%d",
           ip, ntohs(sess->data_addr.sin_port));
 
  sess->data_sock = data_fd;
  return data_fd;
}
 
 