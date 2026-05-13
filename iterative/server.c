#include "server.h"
#include "miniftp_log.h"
#include "utils.h"
#include "config.h"
#include "pi.h"
#include "session.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>

extern int server_socket;

int server_init(const char *ip, int port) {
  struct sockaddr_in server_addr;

  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    log_error("Error creating socket: %s", strerror(errno));
    return -1;
  }

  const int opt = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    log_error("Error setting SO_REUSEADDR: %s", strerror(errno));
    close(listen_fd);
    return -1;
  }

#ifdef SO_REUSEPORT
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
    log_error("Error setting SO_REUSEPORT: %s", strerror(errno));
    close(listen_fd);
    return -1;
  }
#endif

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);

  if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
    log_error("Invalid IP address: %s", ip);
    close(listen_fd);
    return -1;
  }

  if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    log_error("Bind failed: %s", strerror(errno));
    close(listen_fd);
    return -1;
  }

  char ip_buf[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &server_addr.sin_addr, ip_buf, sizeof(ip_buf));
  log_info("Listening on %s:%d", ip_buf, port);

  if (listen(listen_fd, SOMAXCONN) < 0) {
    log_error("Listen failed: %s", strerror(errno));
    close(listen_fd);
    return -1;
  }

  server_socket = listen_fd;
  return listen_fd;
}

int server_accept(int listen_fd, struct sockaddr_in *client_addr) {
  socklen_t addrlen = sizeof(*client_addr);
  int new_socket = accept(listen_fd, (struct sockaddr *)client_addr, &addrlen);

  if (new_socket < 0 && errno != EINTR) {
    log_error("Accept failed: %s", strerror(errno));
    return -1;
  }

  return new_socket;
}

void server_loop(int socket) {
  session_init(socket);

  if (welcome(current_sess) < 0)
    return;

  while(1) {
    if (getexe_command(current_sess) < 0)
      break;
  }

  session_cleanup();
}