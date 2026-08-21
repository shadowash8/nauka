#include "config.h"
#include "decoration.h"
#include "keyboard.h"
#include "layer_shell.h"
#include "layout.h"
#include "output.h"
#include "pointer.h"
#include "tags.h"
#include "toplevel.h"
#include <wlr/backend.h>
#include <wlr/util/box.h>

struct nauka_server {
  struct wl_display *wl_display;
  struct wlr_backend *backend;
  struct wlr_session *session;
  struct wlr_renderer *renderer;
  struct wlr_allocator *allocator;
  struct wlr_scene *scene;
  struct wlr_scene_output_layout *scene_layout;

  struct wlr_xdg_shell *xdg_shell;
  struct wl_listener new_xdg_toplevel;
  struct wl_listener new_xdg_popup;
  struct wl_list toplevels;

  struct wlr_layer_shell_v1 *layer_shell;
  struct wl_listener new_layer_surface;

  struct wlr_scene_tree *background_tree;
  struct wlr_scene_tree *bottom_tree;
  struct wlr_scene_tree *toplevel_tree;
  struct wlr_scene_tree *top_tree;
  struct wlr_scene_tree *overlay_tree;

  struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
  struct wl_listener new_toplevel_decoration;

  struct wlr_cursor *cursor;
  struct wlr_xcursor_manager *cursor_mgr;
  struct wl_listener cursor_motion;
  struct wl_listener cursor_motion_absolute;
  struct wl_listener cursor_button;
  struct wl_listener cursor_axis;
  struct wl_listener cursor_frame;

  struct wlr_seat *seat;
  struct wl_listener new_input;
  struct wl_listener request_cursor;
  struct wl_listener pointer_focus_change;
  struct wl_listener request_set_selection;
  struct wl_list keyboards;
  enum nauka_cursor_mode cursor_mode;
  struct nauka_toplevel *grabbed_toplevel;
  double grab_x, grab_y;
  struct wlr_box grab_geobox;
  uint32_t resize_edges;

  struct wlr_output_layout *output_layout;
  struct wl_list outputs;
  struct wl_listener new_output;

  struct nauka_config config;
  int current_tag;
};
