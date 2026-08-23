#include "nauka.h"
#include <wlr/types/wlr_xdg_decoration_v1.h>

extern struct nauka_server server;
void server_new_toplevel_decoration(struct wl_listener *listener, void *data) {
  struct wlr_xdg_toplevel_decoration_v1 *decoration = data;

  if (!decoration->toplevel->base->initialized) {
    return;
  }

  wlr_xdg_toplevel_decoration_v1_set_mode(
      decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void decoration_destroy() { return; }
