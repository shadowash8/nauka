#include "ipc.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s message\n"
          "where message is one of\n"
          "  subscribe - receive all the events from the compositor\n"
          "  toplevels - list app_ids and titles of all the toplevels\n"
          "  layers    - list namespaces of all the layers\n"
          "  outputs   - list names of all the outputs\n",
          argv0);
}

static bool valid_message(const char *message) {
  return strcmp(message, "subscribe") == 0 ||
         strcmp(message, "toplevels") == 0 ||
         strcmp(message, "layers") == 0 ||
         strcmp(message, "outputs") == 0;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    usage(argv[0]);
    return 1;
  }

  const char *message = argv[1];
  if (!valid_message(message)) {
    usage(argv[0]);
    return 1;
  }

  char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
  nauka_ipc_socket_path(path, sizeof(path));

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("nauka-ipc: socket");
    return 1;
  }

  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    fprintf(stderr, "nauka-ipc: cannot connect to %s: %s\n", path,
            strerror(errno));
    close(fd);
    return 1;
  }

  signal(SIGPIPE, SIG_IGN);

  char request[64];
  int request_len = snprintf(request, sizeof(request), "%s\n", message);
  if (send(fd, request, (size_t)request_len, MSG_NOSIGNAL) != request_len) {
    perror("nauka-ipc: send");
    close(fd);
    return 1;
  }

  char buf[4096];
  for (;;) {
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      perror("nauka-ipc: recv");
      close(fd);
      return 1;
    }
    if (n == 0)
      break;

    if (fwrite(buf, 1, (size_t)n, stdout) != (size_t)n) {
      perror("nauka-ipc: write");
      close(fd);
      return 1;
    }
    fflush(stdout);
  }

  close(fd);
  return 0;
}
