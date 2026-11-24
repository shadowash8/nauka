#include "nauka.h"
#include <stdlib.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>

static void idle_inhibitor_destroy(struct wl_listener *listener, void *data) {
  struct nauka_idle_inhibitor *inh = wl_container_of(listener, inh, destroy);
  struct nauka_server *server = inh->server;
  wl_list_remove(&inh->destroy.link);
  server->idle_inhibitor_count--;
  idle_update_inhibited(server);
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
  idle_update_inhibited(server);
}

void idle_notify_activity(struct nauka_server *server) {
  wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
}

void idle_update_inhibited(struct nauka_server *server) {
  if (server->idle_notifier == NULL)
    return;

  bool inhibited = server->idle_inhibitor_count > 0;

  if (!inhibited) {
    struct nauka_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
      if (toplevel->is_fullscreen && toplevel_is_visible(toplevel)) {
        inhibited = true;
        break;
      }
    }
  }

  if (inhibited == server->idle_inhibited)
    return;

  server->idle_inhibited = inhibited;
  wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, inhibited);
}
