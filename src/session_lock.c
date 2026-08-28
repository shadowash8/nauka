#include "nauka.h"
#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/util/log.h>

static const float lock_bg_color[4] = {0.f, 0.f, 0.f, 1.f};

void session_lock_create_output_state(struct nauka_output *output) {
  struct nauka_server *server = output->server;

  output->lock_background =
      wlr_scene_rect_create(server->lock_tree, output->wlr_output->width,
                            output->wlr_output->height, lock_bg_color);

  session_lock_update_output(output);
}

void session_lock_update_output(struct nauka_output *output) {
  struct nauka_server *server = output->server;

  if (output->lock_background == NULL) {
    return;
  }

  struct wlr_box box = {0};
  wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);

  wlr_scene_rect_set_size(output->lock_background, box.width, box.height);
  wlr_scene_node_set_position(&output->lock_background->node, box.x, box.y);

  /* Mapped lock surfaces reconfigure themselves via their own
   * output_commit listener (lock_surface_handle_output_commit below), so
   * there's nothing else to do here even while locked. */
}

void session_lock_output_destroy(struct nauka_output *output) {
  /* wlr_scene_rect node is a child of lock_tree, not of any per-output
   * scene subtree, so it needs to be torn down explicitly here. */
  if (output->lock_background != NULL) {
    wlr_scene_node_destroy(&output->lock_background->node);
    output->lock_background = NULL;
  }
  if (output->lock_surface != NULL) {
    output->lock_surface->output = NULL;
    output->lock_surface = NULL;
  }
}

static void lock_surface_handle_destroy(struct wl_listener *listener,
                                        void *data) {
  (void)data;
  struct nauka_session_lock_surface *surf =
      wl_container_of(listener, surf, destroy);

  wl_list_remove(&surf->destroy.link);
  wl_list_remove(&surf->map.link);
  wl_list_remove(&surf->output_commit.link);
  wl_list_remove(&surf->link);

  if (surf->output != NULL) {
    surf->output->lock_surface = NULL;
  }

  wlr_scene_node_destroy(&surf->scene_tree->node);
  free(surf);
}

static void lock_surface_handle_map(struct wl_listener *listener, void *data) {
  (void)data;
  struct nauka_session_lock_surface *surf =
      wl_container_of(listener, surf, map);
  struct nauka_server *server = surf->lock->server;
  struct wlr_seat *seat = server->seat;
  struct wlr_surface *surface = surf->wlr_lock_surface->surface;

  /* If a popup grab or a drag-and-drop operation was holding the seat's
   * keyboard grab at the moment we locked, notify_enter below won't
   * override it and the lock surface's first keystroke can vanish into
   * the stale grab. End it explicitly before taking focus. */
  if (wlr_seat_keyboard_has_grab(seat)) {
    wlr_seat_keyboard_end_grab(seat);
  } else if (seat->keyboard_state.focused_surface == surface) {
    return;
  }

  struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
  wlr_seat_keyboard_notify_enter(seat, surface,
                                 keyboard ? keyboard->keycodes : NULL,
                                 keyboard ? keyboard->num_keycodes : 0,
                                 keyboard ? &keyboard->modifiers : NULL);
}

static void lock_surface_handle_output_commit(struct wl_listener *listener,
                                              void *data) {
  (void)data;
  struct nauka_session_lock_surface *surf =
      wl_container_of(listener, surf, output_commit);

  if (surf->output == NULL) {
    return;
  }
  struct nauka_server *server = surf->lock->server;

  /* effective_resolution (not wlr_output->width/height) accounts for the
   * output's transform, so a rotated monitor gets correctly swapped
   * width/height instead of a stretched lock surface. */
  int width, height;
  wlr_output_effective_resolution(surf->output->wlr_output, &width, &height);
  wlr_session_lock_surface_v1_configure(surf->wlr_lock_surface, (uint32_t)width,
                                        (uint32_t)height);

  struct wlr_box box = {0};
  wlr_output_layout_get_box(server->output_layout, surf->output->wlr_output,
                            &box);
  wlr_scene_node_set_position(&surf->scene_tree->node, box.x, box.y);
}

static void lock_handle_new_surface(struct wl_listener *listener, void *data) {
  struct nauka_session_lock *lock =
      wl_container_of(listener, lock, new_surface);
  struct wlr_session_lock_surface_v1 *wlr_lock_surface = data;
  struct nauka_output *output = wlr_lock_surface->output->data;

  if (output == NULL) {
    wlr_log(WLR_ERROR,
            "lock surface requested for output with no nauka_output");
    return;
  }

  struct nauka_session_lock_surface *surf = calloc(1, sizeof(*surf));
  if (surf == NULL) {
    return;
  }
  surf->lock = lock;
  surf->output = output;
  surf->wlr_lock_surface = wlr_lock_surface;

  surf->scene_tree = wlr_scene_subsurface_tree_create(
      lock->server->lock_tree, wlr_lock_surface->surface);

  /* An output can only have one lock surface at a time per protocol; if
   * the client somehow creates a second one for the same output, the
   * older scene tree is simply orphaned visually behind the new one. */
  output->lock_surface = surf;

  surf->destroy.notify = lock_surface_handle_destroy;
  wl_signal_add(&wlr_lock_surface->events.destroy, &surf->destroy);
  surf->map.notify = lock_surface_handle_map;
  wl_signal_add(&wlr_lock_surface->surface->events.map, &surf->map);
  surf->output_commit.notify = lock_surface_handle_output_commit;
  wl_signal_add(&wlr_lock_surface->output->events.commit, &surf->output_commit);

  wl_list_insert(&lock->surfaces, &surf->link);

  /* Configure once immediately rather than waiting for the next output
   * commit, so the client gets a size right away. */
  lock_surface_handle_output_commit(&surf->output_commit, NULL);
}

static void lock_handle_unlock(struct wl_listener *listener, void *data) {
  (void)data;
  struct nauka_session_lock *lock = wl_container_of(listener, lock, unlock);
  struct nauka_server *server = lock->server;

  server->locked = false;
  wlr_scene_node_set_enabled(&server->lock_tree->node, false);

  if (server->focused_toplevel != NULL) {
    focus_toplevel(server->focused_toplevel);
  } else {
    wlr_seat_keyboard_notify_clear_focus(server->seat);
  }

  wlr_log(WLR_INFO, "session unlocked");
}

static void lock_handle_destroy(struct wl_listener *listener, void *data) {
  (void)data;
  struct nauka_session_lock *lock = wl_container_of(listener, lock, destroy);
  struct nauka_server *server = lock->server;

  wl_list_remove(&lock->new_surface.link);
  wl_list_remove(&lock->unlock.link);
  wl_list_remove(&lock->destroy.link);

  if (server->session_lock == lock) {
    server->session_lock = NULL;

    /* Per the ext-session-lock-v1 protocol, if the client disconnects
     * without sending unlock_and_destroy, the session MUST stay locked --
     * dropping the lock object is not the same as unlocking. lock_tree
     * (and its backdrop rects) stays enabled; only a subsequent lock
     * client's unlock() actually clears server->locked. */
    if (server->locked) {
      wlr_log(WLR_INFO, "session lock client gone while still locked; "
                        "screen remains locked");
    }
  }

  free(lock);
}

void server_new_session_lock(struct wl_listener *listener, void *data) {
  struct nauka_server *server =
      wl_container_of(listener, server, new_session_lock);
  struct wlr_session_lock_v1 *wlr_lock = data;

  if (server->session_lock != NULL) {
    /* Only one lock may be active at a time. Refusing by destroying the
     * new lock object is explicitly allowed by the protocol; the client
     * sees its lock request fail. */
    wlr_log(WLR_ERROR, "rejecting session lock request: already locked");
    wlr_session_lock_v1_destroy(wlr_lock);
    return;
  }

  struct nauka_session_lock *lock = calloc(1, sizeof(*lock));
  if (lock == NULL) {
    wlr_session_lock_v1_destroy(wlr_lock);
    return;
  }
  lock->server = server;
  lock->wlr_lock = wlr_lock;
  wl_list_init(&lock->surfaces);

  lock->new_surface.notify = lock_handle_new_surface;
  wl_signal_add(&wlr_lock->events.new_surface, &lock->new_surface);
  lock->unlock.notify = lock_handle_unlock;
  wl_signal_add(&wlr_lock->events.unlock, &lock->unlock);
  lock->destroy.notify = lock_handle_destroy;
  wl_signal_add(&wlr_lock->events.destroy, &lock->destroy);

  server->session_lock = lock;
  server->locked = true;

  wlr_scene_node_set_enabled(&server->lock_tree->node, true);
  wlr_scene_node_raise_to_top(&server->lock_tree->node);

  /* Drop focus from whatever toplevel had it; a lock surface will claim
   * keyboard focus itself once it maps (lock_surface_handle_map). Until
   * then no surface has focus, so nothing leaks keystrokes. */
  wlr_seat_keyboard_notify_clear_focus(server->seat);

  wlr_session_lock_v1_send_locked(wlr_lock);
  wlr_log(WLR_INFO, "session locked");
}
