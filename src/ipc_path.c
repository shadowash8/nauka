#include "ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

const char *nauka_ipc_socket_path(char *buf, size_t len) {
  const char *override = getenv("NAUKA_IPC_SOCKET");
  if (override != NULL && override[0] != '\0') {
    snprintf(buf, len, "%s", override);
    return buf;
  }

  const char *runtime = getenv("XDG_RUNTIME_DIR");
  if (runtime != NULL && runtime[0] != '\0') {
    snprintf(buf, len, "%s/nauka-ipc.sock", runtime);
  } else {
    snprintf(buf, len, "/tmp/nauka-ipc-%u.sock", (unsigned)getuid());
  }

  return buf;
}
