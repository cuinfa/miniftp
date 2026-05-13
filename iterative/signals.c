// signal.c

#include "miniftp_log.h"
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdio.h>
#include "signals.h"
#include "session.h"
#include "utils.h"
#include <errno.h>
#include <string.h>

int server_socket = -1;

static void handle_sigint(int sig) {
  (void)sig;
  static volatile sig_atomic_t in_handler = 0;

  if (in_handler) {
    log_warn("SIGINT handler reentered!");
    return;
  }
  in_handler = 1;

  static int sigint_count = 0;
  log_warn("SIGINT handler called (count = %d) in PID %d", ++sigint_count, getpid());
  log_info("SIGINT received. Shutting down...");

  if (server_socket >= 0) {
    close_fd(server_socket, "listen socket");
    server_socket = -1;
  }

  sigset_t blockset, oldset;
  sigemptyset(&blockset);
  sigaddset(&blockset, SIGINT);
  if (sigprocmask(SIG_BLOCK, &blockset, &oldset) < 0) {
    log_error("sigprocmask: %s", strerror(errno));
  }

  sigprocmask(SIG_SETMASK, &oldset, NULL);

  exit(EXIT_SUCCESS);
}

static void handle_sigterm(int sig) {
  (void)sig;

  static volatile sig_atomic_t in_handler = 0;
  if (in_handler) {
    log_warn("SIGTERM handler reentered!");
    return;
  }
  in_handler = 1;

  log_info("SIGTERM received. Shutting down (PID %d)...", getpid());

  if (server_socket >= 0) {
    close_fd(server_socket, "listen socket");
    server_socket = -1;
  }

  exit(EXIT_SUCCESS);
}

void setup_signals(void) {
  struct sigaction sa;

  log_info("Setting up signal handlers in PID %d", getpid());

  sigemptyset(&sa.sa_mask);
  sigaddset(&sa.sa_mask, SIGINT);
  sa.sa_flags = SA_RESTART;
  sa.sa_handler = handle_sigint;

  if (sigaction(SIGINT, &sa, NULL) == -1) {
    log_error("sigaction SIGINT: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
  log_info("SIGINT handler installed in PID %d", getpid());

  sa.sa_handler = handle_sigterm;
  if (sigaction(SIGTERM, &sa, NULL) == -1) {
    log_error("sigaction SIGTERM: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
}

void reset_signals(void) {
  struct sigaction sa;

  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = SIG_DFL;

  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
}