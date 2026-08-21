#include "layer_shell.h"
#include "nauka.h"

#include <stdlib.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

static void layer_surface_handle_commit(struct wl_listener *listener,
                                        void *data) {
  struct nauka_layer_surface *layer_surface =
      wl_container_of(listener, layer_surface, commit);

  struct wlr_layer_surface_v1 *surface = layer_surface->layer_surface;

  if (!surface->initialized) {
    return;
  }

  struct wlr_output *output = surface->output;

  if (output == NULL) {
    return;
  }

  struct nauka_server *server = layer_surface->server;

  struct wlr_box full_area = {0};
  wlr_output_layout_get_box(server->output_layout, output, &full_area);
  full_area.x = 0;
  full_area.y = 0;

  struct wlr_box usable_area = full_area;

  wlr_scene_layer_surface_v1_configure(layer_surface->scene_tree, &full_area,
                                       &usable_area);
}

static void layer_surface_handle_map(struct wl_listener *listener, void *data) {
  (void)data;

  struct nauka_layer_surface *layer_surface =
      wl_container_of(listener, layer_surface, map);

  layer_surface->mapped = true;

  if (layer_surface->layer_surface->current.keyboard_interactive !=
      ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
    seat_focus_surface(layer_surface->server,
                       layer_surface->layer_surface->surface);
  }
}

static void layer_surface_handle_unmap(struct wl_listener *listener,
                                       void *data) {
  (void)data;

  struct nauka_layer_surface *layer_surface =
      wl_container_of(listener, layer_surface, unmap);

  layer_surface->mapped = false;

  struct nauka_server *server = layer_surface->server;
  struct wlr_seat *seat = server->seat;

  if (seat->keyboard_state.focused_surface == layer_surface->surface) {
    wlr_seat_keyboard_clear_focus(seat);

    if (!wl_list_empty(&server->toplevels)) {
      struct nauka_toplevel *top =
          wl_container_of(server->toplevels.next, top, link);
      focus_toplevel(top);
    }
  }
}

static void layer_surface_handle_output_destroy(struct wl_listener *listener,
                                                void *data) {
  (void)data;
  struct nauka_layer_surface *layer_surface =
      wl_container_of(listener, layer_surface, output_destroy);

  /* The output is going away — destroy the layer surface now instead of
   * letting a stray commit dereference a stale/invalid wlr_output later. */
  wlr_layer_surface_v1_destroy(layer_surface->layer_surface);
}

static void layer_surface_handle_destroy(struct wl_listener *listener,
                                         void *data) {
  (void)data;

  struct nauka_layer_surface *layer_surface =
      wl_container_of(listener, layer_surface, destroy);

  wl_list_remove(&layer_surface->commit.link);
  wl_list_remove(&layer_surface->map.link);
  wl_list_remove(&layer_surface->unmap.link);
  wl_list_remove(&layer_surface->destroy.link);
  wl_list_remove(&layer_surface->new_popup.link);
  wl_list_remove(&layer_surface->output_destroy.link);

  free(layer_surface);
}

static void layer_popup_commit(struct wl_listener *listener, void *data) {
  struct nauka_popup *popup = wl_container_of(listener, popup, commit);
  if (popup->xdg_popup->base->initial_commit) {
    wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
  }
}

static void layer_popup_destroy(struct wl_listener *listener, void *data) {
  struct nauka_popup *popup = wl_container_of(listener, popup, destroy);
  wl_list_remove(&popup->commit.link);
  wl_list_remove(&popup->destroy.link);
  free(popup);
}

static void layer_surface_handle_new_popup(struct wl_listener *listener,
                                           void *data) {
  struct nauka_layer_surface *layer_surface =
      wl_container_of(listener, layer_surface, new_popup);
  struct wlr_xdg_popup *xdg_popup = data;

  struct nauka_popup *popup = calloc(1, sizeof(*popup));
  popup->xdg_popup = xdg_popup;

  xdg_popup->base->data = wlr_scene_xdg_surface_create(
      layer_surface->scene_tree->tree, xdg_popup->base);

  popup->commit.notify = layer_popup_commit;
  wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
  popup->destroy.notify = layer_popup_destroy;
  wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

void server_new_layer_surface(struct wl_listener *listener, void *data) {
  struct nauka_server *server =
      wl_container_of(listener, server, new_layer_surface);

  struct wlr_layer_surface_v1 *wlr_layer_surface = data;

  struct nauka_layer_surface *layer_surface = calloc(1, sizeof(*layer_surface));

  layer_surface->server = server;
  layer_surface->layer_surface = wlr_layer_surface;
  layer_surface->surface = wlr_layer_surface->surface;

  if (wlr_layer_surface->output == NULL) {
    if (wl_list_empty(&server->outputs)) {
      wlr_layer_surface_v1_destroy(wlr_layer_surface);
      free(layer_surface);
      return;
    }
    struct nauka_output *first_output =
        wl_container_of(server->outputs.next, first_output, link);
    wlr_layer_surface->output = first_output->wlr_output;
  }

  layer_surface->new_popup.notify = layer_surface_handle_new_popup;
  wl_signal_add(&wlr_layer_surface->events.new_popup,
                &layer_surface->new_popup);

  struct wlr_scene_tree *parent;

  switch (wlr_layer_surface->current.layer) {
  case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
    parent = server->background_tree;
    break;

  case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
    parent = server->bottom_tree;
    break;

  case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
    parent = server->top_tree;
    break;

  case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
    parent = server->overlay_tree;
    break;

  default:
    free(layer_surface);
    return;
  }

  layer_surface->scene_tree =
      wlr_scene_layer_surface_v1_create(parent, wlr_layer_surface);
  wlr_layer_surface->data = layer_surface->scene_tree->tree;

  if (wlr_layer_surface->output == NULL) {
    if (wl_list_empty(&server->outputs)) {
      wlr_layer_surface_v1_destroy(wlr_layer_surface);
      free(layer_surface);
      return;
    }
    struct nauka_output *first_output =
        wl_container_of(server->outputs.next, first_output, link);
    wlr_layer_surface->output = first_output->wlr_output;
  }

  layer_surface->output_destroy.notify = layer_surface_handle_output_destroy;
  wl_signal_add(&wlr_layer_surface->output->events.destroy,
                &layer_surface->output_destroy);

  layer_surface->commit.notify = layer_surface_handle_commit;
  wl_signal_add(&wlr_layer_surface->surface->events.commit,
                &layer_surface->commit);

  layer_surface->map.notify = layer_surface_handle_map;
  wl_signal_add(&wlr_layer_surface->surface->events.map, &layer_surface->map);

  layer_surface->unmap.notify = layer_surface_handle_unmap;
  wl_signal_add(&wlr_layer_surface->surface->events.unmap,
                &layer_surface->unmap);

  layer_surface->destroy.notify = layer_surface_handle_destroy;
  wl_signal_add(&wlr_layer_surface->events.destroy, &layer_surface->destroy);
}
