#include "nauka.h"
#include <stdlib.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

static void
decoration_apply_server_side(struct wlr_xdg_toplevel_decoration_v1 *deco) {
  wlr_xdg_toplevel_decoration_v1_set_mode(
      deco, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void decoration_handle_surface_commit(struct wl_listener *listener,
                                             void *data) {
  (void)data;
  struct nauka_decoration *decoration =
      wl_container_of(listener, decoration, surface_commit);

  if (!decoration->deco->toplevel->base->initialized) {
    return;
  }

  if (decoration->mode_pending) {
    decoration_apply_server_side(decoration->deco);
    decoration->mode_pending = false;
  }

  wl_list_remove(&decoration->surface_commit.link);
}

static void decoration_handle_request_mode(struct wl_listener *listener,
                                           void *data) {
  (void)data;
  struct nauka_decoration *decoration =
      wl_container_of(listener, decoration, request_mode);

  if (decoration->deco->toplevel->base->initialized) {
    decoration_apply_server_side(decoration->deco);
  } else {
    decoration->mode_pending = true;
    decoration->surface_commit.notify = decoration_handle_surface_commit;
    wl_signal_add(&decoration->deco->toplevel->base->surface->events.commit,
                  &decoration->surface_commit);
  }
}

static void decoration_handle_destroy(struct wl_listener *listener,
                                      void *data) {
  (void)data;
  struct nauka_decoration *decoration =
      wl_container_of(listener, decoration, destroy);
  wl_list_remove(&decoration->request_mode.link);
  wl_list_remove(&decoration->destroy.link);
  if (decoration->mode_pending) {
    wl_list_remove(&decoration->surface_commit.link);
  }
  free(decoration);
}

void server_new_toplevel_decoration(struct wl_listener *listener, void *data) {
  (void)listener;
  struct wlr_xdg_toplevel_decoration_v1 *deco = data;

  struct nauka_decoration *decoration = calloc(1, sizeof(*decoration));
  decoration->deco = deco;

  decoration->request_mode.notify = decoration_handle_request_mode;
  wl_signal_add(&deco->events.request_mode, &decoration->request_mode);
  decoration->destroy.notify = decoration_handle_destroy;
  wl_signal_add(&deco->events.destroy, &decoration->destroy);
}
