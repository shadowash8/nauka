#include "layer_shell.h"
#include "nauka.h"

#include <stdlib.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>

static void layer_surface_handle_commit(struct wl_listener *listener,
                                        void *data) {
  struct nauka_layer_surface *layer_surface =
      wl_container_of(listener, layer_surface, commit);

  struct wlr_layer_surface_v1 *surface = layer_surface->layer_surface;

  /*
   * The first commit is the client's initial layer-surface commit.
   * It has no buffer and exists to request a configure event.
   */
  if (!surface->initialized) {
    return;
  }

  struct wlr_output *output = surface->output;

  if (output == NULL) {
    return;
  }

  struct nauka_server *server = layer_surface->server;

  struct wlr_box output_box;
  wlr_output_layout_get_box(server->output_layout, output, &output_box);

  /*
   * Tell the client how large its layer surface should be.
   */
  wlr_layer_surface_v1_configure(surface, output_box.width, output_box.height);
}

static void layer_surface_handle_map(struct wl_listener *listener, void *data) {
  (void)data;

  struct nauka_layer_surface *layer_surface =
      wl_container_of(listener, layer_surface, map);

  layer_surface->mapped = true;
}

static void layer_surface_handle_unmap(struct wl_listener *listener,
                                       void *data) {
  (void)data;

  struct nauka_layer_surface *layer_surface =
      wl_container_of(listener, layer_surface, unmap);

  layer_surface->mapped = false;
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

  free(layer_surface);
}

void server_new_layer_surface(struct wl_listener *listener, void *data) {
  struct nauka_server *server =
      wl_container_of(listener, server, new_layer_surface);

  struct wlr_layer_surface_v1 *wlr_layer_surface = data;

  struct nauka_layer_surface *layer_surface = calloc(1, sizeof(*layer_surface));

  layer_surface->server = server;
  layer_surface->layer_surface = wlr_layer_surface;

  layer_surface->scene_tree = wlr_scene_layer_surface_v1_create(
      &server->scene->tree, wlr_layer_surface);

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
