#include "nauka.h"
#include <stdlib.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>

static void idle_inhibitor_destroy(struct wl_listener *listener, void *data) {
  struct nauka_idle_inhibitor *inh = wl_container_of(listener, inh, destroy);
  struct nauka_server *server = inh->server;
  wl_list_remove(&inh->destroy.link);
  server->idle_inhibitor_count--;
  wlr_idle_notifier_v1_set_inhibited(server->idle_notifier,
                                     server->idle_inhibitor_count > 0);
  free(inh);
}

void server_new_idle_inhibitor(struct wl_listener *listener, void *data) {
  struct nauka_server *server =
      wl_container_of(listener, server, new_idle_inhibitor);
  struct wlr_idle_inhibitor_v1 *wlr_inh = data;

  struct nauka_idle_inhibitor *inh = calloc(1, sizeof(*inh));
  if (!inh)
    return;
  inh->server = server;
  inh->destroy.notify = idle_inhibitor_destroy;
  wl_signal_add(&wlr_inh->events.destroy, &inh->destroy);

  server->idle_inhibitor_count++;
  wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, true);
}

void idle_notify_activity(struct nauka_server *server) {
  wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
}
