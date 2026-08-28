#include "config.h"
#include "decoration.h"
#include "keyboard.h"
#include "layer_shell.h"
#include "layout.h"
#include "output.h"
#include "pointer.h"
#include "rendering.h"
#include "session_lock.h"
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
  pid_t xwayland_satellite_pid;

  struct wlr_xdg_shell *xdg_shell;
  struct wl_listener new_xdg_toplevel;
  struct wl_listener new_xdg_popup;
  struct wl_list toplevels;

  struct wlr_layer_shell_v1 *layer_shell;
  struct wl_listener new_layer_surface;

  struct wlr_scene_tree *background_tree;
  struct wlr_scene_tree *bottom_tree;
  struct wlr_scene_tree *toplevel_tree;
  struct wlr_scene_tree *floating_tree;
  struct wlr_scene_optimized_blur *blur_layer;
  struct wlr_scene_tree *top_tree;
  struct wlr_scene_tree *overlay_tree;
  struct wlr_scene_tree *fullscreen_tree;
  struct wlr_scene_tree *lock_tree;

  struct wlr_session_lock_manager_v1 *session_lock_manager;
  struct wl_listener new_session_lock;
  struct nauka_session_lock *session_lock;
  bool locked;

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
  struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;
  struct wlr_pointer_constraints_v1 *pointer_constraints;
  struct wlr_pointer_constraint_v1 *active_constraint;
  struct wl_listener new_pointer_constraint;
  struct wl_list keyboards;
  struct wlr_keyboard_shortcuts_inhibit_manager_v1
      *kb_shortcuts_inhibit_manager;
  struct wl_listener new_kb_shortcuts_inhibitor;
  enum nauka_cursor_mode cursor_mode;
  struct nauka_toplevel *grabbed_toplevel;
  double grab_x, grab_y;
  struct wlr_box grab_geobox;
  uint32_t resize_edges;
  struct wlr_scene_tree *drag_icon;
  struct wl_listener request_start_drag;
  struct wl_listener start_drag;
  struct wl_listener drag_icon_destroy;

  struct nauka_toplevel *focused_toplevel;
  struct nauka_toplevel *prev_focused;

  struct wlr_output_layout *output_layout;
  struct wl_list outputs;
  struct wl_listener new_output;
  struct wlr_output_manager_v1 *output_manager;
  struct wl_listener output_manager_apply;
  struct wl_listener output_manager_test;

  struct nauka_config config;
  int current_tag;

  struct wlr_ext_workspace_manager_v1 *workspace_manager;
  struct wlr_ext_workspace_group_handle_v1 *workspace_group;
  struct wlr_ext_workspace_handle_v1 *workspaces[NAUKA_TAG_COUNT];

  struct wl_listener workspace_manager_commit;
  struct wl_listener workspace_manager_destroy;
};
