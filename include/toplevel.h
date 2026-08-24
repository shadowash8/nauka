#include <wayland-server-core.h>
#include <wlr/util/box.h>

struct nauka_toplevel {
  struct wl_list link;
  struct nauka_server *server;
  struct wlr_xdg_toplevel *xdg_toplevel;
  struct wlr_scene_tree *scene_tree;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener commit;
  struct wl_listener destroy;
  struct wl_listener request_move;
  struct wl_listener request_resize;
  struct wl_listener request_maximize;
  struct wl_listener request_fullscreen;

  struct wlr_scene_tree *border_tree;

  int tag;
  bool is_fullscreen;
  bool floating;
  int corner_radius;
  float opacity;
  struct wlr_scene_blur *blur;
  struct wlr_scene_rect *border;
  struct wlr_box floating_geometry;
};

struct nauka_popup {
  struct wlr_xdg_popup *xdg_popup;
  struct wl_listener commit;
  struct wl_listener destroy;
};

bool toplevel_is_visible(struct nauka_toplevel *toplevel);

void toplevel_update_borders(struct nauka_toplevel *toplevel);

void toplevel_set_border_color(struct nauka_toplevel *toplevel, bool active);

void server_new_xdg_toplevel(struct wl_listener *listener, void *data);

void update_toplevel_visibility(struct nauka_server *server);

void server_new_xdg_popup(struct wl_listener *listener, void *data);

void toplevel_begin_move(struct nauka_toplevel *toplevel);

void toplevel_begin_resize(struct nauka_toplevel *toplevel, uint32_t edges);

void toplevel_toggle_floating(struct nauka_toplevel *toplevel);

void toplevel_toggle_fullscreen(struct nauka_toplevel *toplevel);

void toplevel_apply_config(struct nauka_server *server);
