#include <wayland-server-core.h>

struct nauka_server;

struct nauka_idle_inhibitor {
  struct nauka_server *server;
  struct wl_listener destroy;
};

void server_new_idle_inhibitor(struct wl_listener *listener, void *data);
void idle_notify_activity(struct nauka_server *server);
