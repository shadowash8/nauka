#include <stdbool.h>
#include <wayland-server-core.h>

#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>

struct nauka_server;

struct nauka_layer_surface {
  struct nauka_server *server;

  struct wlr_layer_surface_v1 *layer_surface;
  struct wlr_scene_layer_surface_v1 *scene_tree;

  struct wl_listener commit;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener destroy;
  struct wl_listener new_popup;

  bool mapped;
};

void server_new_layer_surface(struct wl_listener *listener, void *data);
