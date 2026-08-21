#include <wayland-server-core.h>

struct nauka_output {
  struct wl_list link;
  struct nauka_server *server;
  struct wlr_output *wlr_output;
  struct wl_listener frame;
  struct wl_listener request_state;
  struct wl_listener destroy;
};

void output_frame(struct wl_listener *listener, void *data);

void server_new_output(struct wl_listener *listener, void *data);
