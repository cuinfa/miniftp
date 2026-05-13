#define _GNU_SOURCE
#include "dtp.h"
#include "miniftp_log.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
 
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
 