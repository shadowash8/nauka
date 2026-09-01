#include "nauka.h"
#include <pixman.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <linux/input-event-codes.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

static void constrain_pointer(struct nauka_server *server,
                              struct wlr_pointer_constraint_v1 *constraint) {
  if (server->active_constraint == constraint) {
    return;
  }
  if (server->active_constraint) {
    wlr_pointer_constraint_v1_send_deactivated(server->active_constraint);
  }
  server->active_constraint = constraint;
  if (constraint != NULL) {
    wlr_pointer_constraint_v1_send_activated(constraint);
  }
}

struct nauka_pointer_constraint {
  struct nauka_server *server;
  struct wlr_pointer_constraint_v1 *constraint;
  struct wl_listener destroy;
};

static void handle_constraint_destroy(struct wl_listener *listener,
                                      void *data) {
  struct nauka_pointer_constraint *nc = wl_container_of(listener, nc, destroy);
  struct nauka_server *server = nc->server;

  wl_list_remove(&nc->destroy.link);
  if (server->active_constraint == nc->constraint) {
    server->active_constraint = NULL;
  }
  free(nc);
}

void server_new_pointer_constraint(struct wl_listener *listener, void *data) {
  struct nauka_server *server =
      wl_container_of(listener, server, new_pointer_constraint);
  struct wlr_pointer_constraint_v1 *wlr_constraint = data;

  struct nauka_pointer_constraint *nc = calloc(1, sizeof(*nc));
  nc->server = server;
  nc->constraint = wlr_constraint;
  nc->destroy.notify = handle_constraint_destroy;
  wl_signal_add(&wlr_constraint->events.destroy, &nc->destroy);

  /* If the surface asking for a constraint already has pointer focus,
   * activate immediately instead of waiting for the next focus change. */
  if (server->seat->pointer_state.focused_surface == wlr_constraint->surface) {
    constrain_pointer(server, wlr_constraint);
  }
}

/* Clamp the cursor to stay within the confinement region a client set,
 * e.g. a game menu that still shows the cursor but keeps it in-window. */
static void
confine_pointer_to_region(struct nauka_server *server,
                          struct wlr_pointer_constraint_v1 *constraint) {
  if (!pixman_region32_not_empty(&constraint->region)) {
    return;
  }

  double sx = server->seat->pointer_state.sx;
  double sy = server->seat->pointer_state.sy;

  if (!pixman_region32_contains_point(&constraint->region, (int)sx, (int)sy,
                                      NULL)) {
    pixman_box32_t *box = pixman_region32_extents(&constraint->region);
    double clamped_sx = sx < box->x1 ? box->x1 : (sx > box->x2 ? box->x2 : sx);
    double clamped_sy = sy < box->y1 ? box->y1 : (sy > box->y2 ? box->y2 : sy);
    wlr_cursor_move(server->cursor, NULL, clamped_sx - sx, clamped_sy - sy);
  }
}

static void server_new_pointer(struct nauka_server *server,
                               struct wlr_input_device *device) {
  wlr_cursor_attach_input_device(server->cursor, device);

  if (!wlr_input_device_is_libinput(device))
    return;

  struct libinput_device *libinput_dev = wlr_libinput_get_device_handle(device);

  if (libinput_device_config_tap_get_finger_count(libinput_dev) > 0) {
    libinput_device_config_tap_set_enabled(libinput_dev,
                                           LIBINPUT_CONFIG_TAP_ENABLED);
  }
}

void server_new_input(struct wl_listener *listener, void *data) {
  /* This event is raised by the backend when a new input device becomes
   * available. */
  struct nauka_server *server = wl_container_of(listener, server, new_input);
  struct wlr_input_device *device = data;
  switch (device->type) {
  case WLR_INPUT_DEVICE_KEYBOARD:
    server_new_keyboard(server, device);
    break;
  case WLR_INPUT_DEVICE_POINTER:
    server_new_pointer(server, device);
    break;
  default:
    break;
  }
  /* We need to let the wlr_seat know what our capabilities are, which is
   * communiciated to the client. In TinyWL we always have a cursor, even if
   * there are no pointer devices, so we always include that capability. */
  uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
  if (!wl_list_empty(&server->keyboards)) {
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  }
  wlr_seat_set_capabilities(server->seat, caps);
}

void seat_request_cursor(struct wl_listener *listener, void *data) {
  struct nauka_server *server =
      wl_container_of(listener, server, request_cursor);
  /* This event is raised by the seat when a client provides a cursor image */
  struct wlr_seat_pointer_request_set_cursor_event *event = data;
  struct wlr_seat_client *focused_client =
      server->seat->pointer_state.focused_client;
  /* This can be sent by any client, so we check to make sure this one is
   * actually has pointer focus first. */
  if (focused_client == event->seat_client) {
    /* Once we've vetted the client, we can tell the cursor to use the
     * provided surface as the cursor image. It will set the hardware cursor
     * on the output that it's currently on and continue to do so as the
     * cursor moves between outputs. */
    wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x,
                           event->hotspot_y);
  }
}

void seat_pointer_focus_change(struct wl_listener *listener, void *data) {
  struct nauka_server *server =
      wl_container_of(listener, server, pointer_focus_change);
  struct wlr_seat_pointer_focus_change_event *event = data;
  if (event->new_surface == NULL) {
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
  }

  struct wlr_pointer_constraint_v1 *constraint = NULL;
  if (event->new_surface) {
    constraint = wlr_pointer_constraints_v1_constraint_for_surface(
        server->pointer_constraints, event->new_surface, server->seat);
  }
  constrain_pointer(server, constraint);
}

void seat_request_set_selection(struct wl_listener *listener, void *data) {
  /* This event is raised by the seat when a client wants to set the selection,
   * usually when the user copies something. wlroots allows compositors to
   * ignore such requests if they so choose, but in nauka we always honor
   */
  struct nauka_server *server =
      wl_container_of(listener, server, request_set_selection);
  struct wlr_seat_request_set_selection_event *event = data;
  wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static struct nauka_toplevel *desktop_toplevel_at(struct nauka_server *server,
                                                  double lx, double ly,
                                                  struct wlr_surface **surface,
                                                  double *sx, double *sy) {
  /* This returns the topmost node in the scene at the given layout coords.
   * We only care about surface nodes as we are specifically looking for a
   * surface in the surface tree of a nauka_toplevel. */
  struct wlr_scene_node *node =
      wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
  if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
    return NULL;
  }
  struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
  struct wlr_scene_surface *scene_surface =
      wlr_scene_surface_try_from_buffer(scene_buffer);
  if (!scene_surface) {
    return NULL;
  }

  *surface = scene_surface->surface;
  /* Find the node corresponding to the nauka_toplevel at the root of this
   * surface tree, it is the only one for which we set the data field. */
  struct wlr_scene_tree *tree = node->parent;
  while (tree != NULL && tree->node.data == NULL) {
    tree = tree->node.parent;
  }
  if (tree == NULL) {
    return NULL;
  }
  return tree->node.data;
}

void reset_cursor_mode(struct nauka_server *server) {
  /* Reset the cursor mode to passthrough. */
  server->cursor_mode = NAUKA_CURSOR_PASSTHROUGH;
  server->grabbed_toplevel = NULL;
}

static void process_cursor_move(struct nauka_server *server) {
  /* Move the grabbed toplevel to the new position. */
  struct nauka_toplevel *toplevel = server->grabbed_toplevel;
  wlr_scene_node_set_position(&toplevel->scene_tree->node,
                              server->cursor->x - server->grab_x,
                              server->cursor->y - server->grab_y);
}

static void process_cursor_resize(struct nauka_server *server) {
  /*
   * Resizing the grabbed toplevel can be a little bit complicated, because we
   * could be resizing from any corner or edge. This not only resizes the
   * toplevel on one or two axes, but can also move the toplevel if you resize
   * from the top or left edges (or top-left corner).
   *
   * Note that some shortcuts are taken here. In a more fleshed-out
   * compositor, you'd wait for the client to prepare a buffer at the new
   * size, then commit any movement that was prepared.
   */
  struct nauka_toplevel *toplevel = server->grabbed_toplevel;
  double border_x = server->cursor->x - server->grab_x;
  double border_y = server->cursor->y - server->grab_y;
  int new_left = server->grab_geobox.x;
  int new_right = server->grab_geobox.x + server->grab_geobox.width;
  int new_top = server->grab_geobox.y;
  int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

  if (server->resize_edges & WLR_EDGE_TOP) {
    new_top = border_y;
    if (new_top >= new_bottom) {
      new_top = new_bottom - 1;
    }
  } else if (server->resize_edges & WLR_EDGE_BOTTOM) {
    new_bottom = border_y;
    if (new_bottom <= new_top) {
      new_bottom = new_top + 1;
    }
  }
  if (server->resize_edges & WLR_EDGE_LEFT) {
    new_left = border_x;
    if (new_left >= new_right) {
      new_left = new_right - 1;
    }
  } else if (server->resize_edges & WLR_EDGE_RIGHT) {
    new_right = border_x;
    if (new_right <= new_left) {
      new_right = new_left + 1;
    }
  }

  struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
  wlr_scene_node_set_position(&toplevel->scene_tree->node,
                              new_left - geo_box->x, new_top - geo_box->y);

  int new_width = new_right - new_left;
  int new_height = new_bottom - new_top;
  wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
}

static void handle_drag_icon_destroy(struct wl_listener *listener, void *data) {
  struct nauka_server *server =
      wl_container_of(listener, server, drag_icon_destroy);
  wl_list_remove(&server->drag_icon_destroy.link);
  server->drag_icon = NULL;
}

void seat_request_start_drag(struct wl_listener *listener, void *data) {
  struct nauka_server *server =
      wl_container_of(listener, server, request_start_drag);
  struct wlr_seat_request_start_drag_event *event = data;

  if (wlr_seat_validate_pointer_grab_serial(server->seat, event->origin,
                                            event->serial)) {
    wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
    return;
  }

  /* Reject drags with a stale/invalid serial, per wlroots' recommendation,
   * so the client doesn't hang waiting for a drag that'll never start. */
  if (event->drag->source != NULL) {
    wlr_data_source_destroy(event->drag->source);
  }
}

void seat_start_drag(struct wl_listener *listener, void *data) {
  struct nauka_server *server = wl_container_of(listener, server, start_drag);
  struct wlr_drag *drag = data;
  if (!drag->icon) {
    return; /* some drags have no visual icon, that's valid */
  }

  server->drag_icon =
      wlr_scene_drag_icon_create(&server->scene->tree, drag->icon);
  wlr_scene_node_set_position(&server->drag_icon->node, server->cursor->x,
                              server->cursor->y);

  server->drag_icon_destroy.notify = handle_drag_icon_destroy;
  wl_signal_add(&drag->icon->events.destroy, &server->drag_icon_destroy);
}

static void process_cursor_motion(struct nauka_server *server, uint32_t time) {
  /* If the mode is non-passthrough, delegate to those functions. */
  if (server->cursor_mode == NAUKA_CURSOR_MOVE) {
    process_cursor_move(server);
    return;
  } else if (server->cursor_mode == NAUKA_CURSOR_RESIZE) {
    process_cursor_resize(server);
    return;
  }
  if (server->drag_icon != NULL) {
    wlr_scene_node_set_position(&server->drag_icon->node, server->cursor->x,
                                server->cursor->y);
  }

  /* Otherwise, find the toplevel under the pointer and send the event along. */
  double sx, sy;
  struct wlr_seat *seat = server->seat;
  struct wlr_surface *surface = NULL;
  struct nauka_toplevel *toplevel = desktop_toplevel_at(
      server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
  if (!toplevel) {
    /* If there's no toplevel under the cursor, set the cursor image to a
     * default. This is what makes the cursor image appear when you move it
     * around the screen, not over any toplevels. */
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
  }
  if (server->config.focus_follows_mouse && toplevel != NULL &&
      toplevel != server->focused_toplevel) {
    focus_toplevel(toplevel);
  }
  if (surface) {
    /*
     * Send pointer enter and motion events.
     *
     * The enter event gives the surface "pointer focus", which is distinct
     * from keyboard focus. You get pointer focus by moving the pointer over
     * a window.
     *
     * Note that wlroots will avoid sending duplicate enter/motion events if
     * the surface has already has pointer focus or if the client is already
     * aware of the coordinates passed.
     */
    wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(seat, time, sx, sy);
  } else {
    /* Clear pointer focus so future button events and such are not sent to
     * the last client to have the cursor over it. */
    wlr_seat_pointer_clear_focus(seat);
  }
}

void server_cursor_motion(struct wl_listener *listener, void *data) {
  struct nauka_server *server =
      wl_container_of(listener, server, cursor_motion);
  struct wlr_pointer_motion_event *event = data;

  /* Always deliver raw relative deltas to clients using the relative
   * pointer protocol — this is what gives Minecraft/GLFW smooth mouse-look
   * instead of accel-based fallback motion. */
  wlr_relative_pointer_manager_v1_send_relative_motion(
      server->relative_pointer_manager, server->seat,
      (uint64_t)event->time_msec * 1000, event->delta_x, event->delta_y,
      event->unaccel_dx, event->unaccel_dy);

  struct wlr_pointer_constraint_v1 *constraint = server->active_constraint;
  if (constraint && constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
    /* Locked: cursor position stays frozen, client only sees relative
     * deltas above. This is the mode Minecraft's mouse-look actually uses. */
    return;
  }

  wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x,
                  event->delta_y);

  if (constraint && constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED) {
    confine_pointer_to_region(server, constraint);
  }

  process_cursor_motion(server, event->time_msec);
}

void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits an _absolute_
   * motion event, from 0..1 on each axis. This happens, for example, when
   * wlroots is running under a Wayland window rather than KMS+DRM, and you
   * move the mouse over the window. You could enter the window from any edge,
   * so we have to warp the mouse there. There is also some hardware which
   * emits these events. */
  struct nauka_server *server =
      wl_container_of(listener, server, cursor_motion_absolute);
  struct wlr_pointer_motion_absolute_event *event = data;
  wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x,
                           event->y);
  process_cursor_motion(server, event->time_msec);
}

void server_cursor_button(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits a button
   * event. */
  struct nauka_server *server =
      wl_container_of(listener, server, cursor_button);
  struct wlr_pointer_button_event *event = data;
  if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
    /* If you released any buttons, we exit interactive move/resize mode. */
    wlr_seat_pointer_notify_button(server->seat, event->time_msec,
                                   event->button, event->state);
    bool was_moving_or_resizing =
        server->cursor_mode != NAUKA_CURSOR_PASSTHROUGH;
    reset_cursor_mode(server);

    if (was_moving_or_resizing) {
      arrange_windows(server);
    }
    return;
  }

  double sx, sy;
  struct wlr_surface *surface = NULL;
  struct nauka_toplevel *toplevel = desktop_toplevel_at(
      server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

  struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
  uint32_t modifiers = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;

  if (toplevel && !toplevel->is_fullscreen &&
      (modifiers & server->config.mod) == server->config.mod) {

    if (event->button == server->config.move_button) {
      focus_toplevel(toplevel);
      toplevel_begin_move(toplevel);
      return;
    } else if (event->button == server->config.resize_button) {
      focus_toplevel(toplevel);
      struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
      double win_x = toplevel->scene_tree->node.x + geo->x;
      double win_y = toplevel->scene_tree->node.y + geo->y;

      uint32_t edges = 0;
      edges |= (server->cursor->x < win_x + geo->width / 2) ? WLR_EDGE_LEFT
                                                            : WLR_EDGE_RIGHT;
      edges |= (server->cursor->y < win_y + geo->height / 2) ? WLR_EDGE_TOP
                                                             : WLR_EDGE_BOTTOM;

      toplevel_begin_resize(toplevel, edges);
    }
    return; /* don't forward the click to the client */
  }

  /* Notify the client with pointer focus that a button press has occurred */
  wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button,
                                 event->state);

  focus_toplevel(toplevel);
}

void server_cursor_axis(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits an axis event,
   * for example when you move the scroll wheel. */
  struct nauka_server *server = wl_container_of(listener, server, cursor_axis);
  struct wlr_pointer_axis_event *event = data;
  /* Notify the client with pointer focus of the axis event. */
  wlr_seat_pointer_notify_axis(
      server->seat, event->time_msec, event->orientation, event->delta,
      event->delta_discrete, event->source, event->relative_direction);
}

void server_cursor_frame(struct wl_listener *listener, void *data) {
  /* This event is forwarded by the cursor when a pointer emits an frame
   * event. Frame events are sent after regular pointer events to group
   * multiple events together. For instance, two axis events may happen at the
   * same time, in which case a frame event won't be sent in between. */
  struct nauka_server *server = wl_container_of(listener, server, cursor_frame);
  /* Notify the client with pointer focus of the frame event. */
  wlr_seat_pointer_notify_frame(server->seat);
}

void pointer_reload_theme(struct nauka_server *server) {
  if (server->cursor_mgr) {
    wlr_xcursor_manager_destroy(server->cursor_mgr);
  }

  server->cursor_mgr = wlr_xcursor_manager_create(server->config.cursor_theme,
                                                  server->config.cursor_size);

  struct nauka_output *output;
  wl_list_for_each(output, &server->outputs, link) {
    wlr_xcursor_manager_load(server->cursor_mgr, output->wlr_output->scale);
  }

  wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");

  char size[16];
  snprintf(size, sizeof(size), "%u", server->config.cursor_size);
  setenv("XCURSOR_THEME", server->config.cursor_theme, 1);
  setenv("XCURSOR_SIZE", size, 1);
}
