#include <wayland-server-core.h>

struct nauka_output {
  struct wl_list link;
  struct nauka_server *server;
  struct wlr_output *wlr_output;
  struct wl_listener frame;
  struct wl_listener request_state;
  struct wl_listener destroy;

  struct wlr_box usable_area;
  struct wl_list layers;

  struct wlr_scene_rect *lock_background;
  struct nauka_session_lock_surface *lock_surface;
};

void output_frame(struct wl_listener *listener, void *data);

void server_new_output(struct wl_listener *listener, void *data);

void update_output_manager_config(struct nauka_server *server);
void output_manager_apply(struct wl_listener *listener, void *data);
void output_manager_test(struct wl_listener *listener, void *data);
