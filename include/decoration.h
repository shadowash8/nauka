#include <wayland-server-core.h>

struct nauka_decoration {
  struct wlr_xdg_toplevel_decoration_v1 *deco;
  struct wl_listener request_mode;
  struct wl_listener destroy;
  struct wl_listener surface_commit;
  bool mode_pending;
};

void server_new_toplevel_decoration(struct wl_listener *listener, void *data);
