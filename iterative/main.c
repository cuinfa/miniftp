#include "arguments.h"
#include "miniftp_log.h"
#include "server.h"
#include "utils.h"
#include "signals.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

int main(int argc, char **argv) {
  log_init("miniftp", 1, NULL);

  struct arguments args;

  if (parse_arguments(argc, argv, &args) != 0)
    return EXIT_FAILURE;

  log_info("Starting server on %s:%d", args.address, args.port);

  int listen_fd = server_init(args.address, args.port);
  if (listen_fd < 0)
    return EXIT_FAILURE;

  setup_signals();

  while(1) {
    struct sockaddr_in client_addr;
    int new_socket = server_accept(listen_fd, &client_addr);
    if (new_socket < 0)
      continue;

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    log_info("Connection from %s:%d accepted", client_ip, ntohs(client_addr.sin_port));

    server_loop(new_socket);

    log_info("Connection from %s:%d closed", client_ip, ntohs(client_addr.sin_port));
  }

  // NEVER GO HERE
  close_fd(listen_fd, "listening socket");

  log_close();
  return EXIT_SUCCESS;
}