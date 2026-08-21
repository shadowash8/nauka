#include "nauka.h"
#include <assert.h>
#include <stdlib.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

void toplevel_update_borders(struct nauka_toplevel *toplevel) {
  struct nauka_config *cfg = &toplevel->server->config;

  struct wlr_box geo = toplevel->xdg_toplevel->base->geometry;

  int w = geo.width;
  int h = geo.height;
  int bw = cfg->border_width;

  /* Top */
  wlr_scene_rect_set_size(toplevel->border[0], w + 2 * bw, bw);

  wlr_scene_node_set_position(&toplevel->border[0]->node, geo.x - bw,
                              geo.y - bw);

  /* Bottom */
  wlr_scene_rect_set_size(toplevel->border[1], w + 2 * bw, bw);

  wlr_scene_node_set_position(&toplevel->border[1]->node, geo.x - bw,
                              geo.y + h);

  /* Left */
  wlr_scene_rect_set_size(toplevel->border[2], bw, h);

  wlr_scene_node_set_position(&toplevel->border[2]->node, geo.x - bw, geo.y);

  /* Right */
  wlr_scene_rect_set_size(toplevel->border[3], bw, h);

  wlr_scene_node_set_position(&toplevel->border[3]->node, geo.x + w, geo.y);
}

void toplevel_set_border_color(struct nauka_toplevel *toplevel, bool active) {
  struct nauka_config *cfg = &toplevel->server->config;
  const float *color =
      active ? cfg->border_color_active : cfg->border_color_inactive;
  for (int i = 0; i < 4; i++) {
    wlr_scene_rect_set_color(toplevel->border[i], color);
  }
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
  /* Called when the surface is mapped, or ready to display on-screen. */
  struct nauka_toplevel *toplevel = wl_container_of(listener, toplevel, map);

  wl_list_insert(&toplevel->server->toplevels, &toplevel->link);

  /* Center the new toplevel on the output it'll first appear on. */
  struct wlr_box box = {0};
  struct wlr_output *output = wlr_output_layout_output_at(
      toplevel->server->output_layout, toplevel->server->cursor->x,
      toplevel->server->cursor->y);
  if (output != NULL) {
    wlr_output_layout_get_box(toplevel->server->output_layout, output, &box);
  }

  struct wlr_box geo = toplevel->xdg_toplevel->base->geometry;
  int width = geo.width > 0 ? geo.width : box.width;
  int height = geo.height > 0 ? geo.height : box.height;

  wlr_scene_node_set_position(&toplevel->scene_tree->node,
                              box.x + (box.width - width) / 2,
                              box.y + (box.height - height) / 2);

  toplevel_update_borders(toplevel);
  focus_toplevel(toplevel);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
  /* Called when the surface is unmapped, and should no longer be shown. */
  struct nauka_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);

  /* Reset the cursor mode if the grabbed toplevel was unmapped. */
  if (toplevel == toplevel->server->grabbed_toplevel) {
    reset_cursor_mode(toplevel->server);
  }

  wl_list_remove(&toplevel->link);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
  /* Called when a new surface state is committed. */
  struct nauka_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

  if (toplevel->xdg_toplevel->base->initial_commit) {
    /* When an xdg_surface performs an initial commit, the compositor must
     * reply with a configure so the client can map the surface. nauka
     * configures the xdg_toplevel with 0,0 size to let the client pick the
     * dimensions itself. */
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
  }
  toplevel_update_borders(toplevel);
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
  /* Called when the xdg_toplevel is destroyed. */
  struct nauka_toplevel *toplevel =
      wl_container_of(listener, toplevel, destroy);

  wl_list_remove(&toplevel->map.link);
  wl_list_remove(&toplevel->unmap.link);
  wl_list_remove(&toplevel->commit.link);
  wl_list_remove(&toplevel->destroy.link);
  wl_list_remove(&toplevel->request_move.link);
  wl_list_remove(&toplevel->request_resize.link);
  wl_list_remove(&toplevel->request_maximize.link);
  wl_list_remove(&toplevel->request_fullscreen.link);

  free(toplevel);
}

static void begin_interactive(struct nauka_toplevel *toplevel,
                              enum nauka_cursor_mode mode, uint32_t edges) {
  /* This function sets up an interactive move or resize operation, where the
   * compositor stops propagating pointer events to clients and instead
   * consumes them itself, to move or resize windows. */
  struct nauka_server *server = toplevel->server;

  server->grabbed_toplevel = toplevel;
  server->cursor_mode = mode;

  if (mode == NAUKA_CURSOR_MOVE) {
    server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
    server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
  } else {
    struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;

    double border_x = (toplevel->scene_tree->node.x + geo_box->x) +
                      ((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
    double border_y = (toplevel->scene_tree->node.y + geo_box->y) +
                      ((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);
    server->grab_x = server->cursor->x - border_x;
    server->grab_y = server->cursor->y - border_y;

    server->grab_geobox = *geo_box;
    server->grab_geobox.x += toplevel->scene_tree->node.x;
    server->grab_geobox.y += toplevel->scene_tree->node.y;

    server->resize_edges = edges;
  }
}

void toplevel_begin_move(struct nauka_toplevel *toplevel) {
  begin_interactive(toplevel, NAUKA_CURSOR_MOVE, 0);
}

void toplevel_begin_resize(struct nauka_toplevel *toplevel, uint32_t edges) {
  begin_interactive(toplevel, NAUKA_CURSOR_RESIZE, edges);
}

static void xdg_toplevel_request_move(struct wl_listener *listener,
                                      void *data) {
  /* This event is raised when a client would like to begin an interactive
   * move, typically because the user clicked on their client-side
   * decorations. Note that a more sophisticated compositor should check the
   * provided serial against a list of button press serials sent to this
   * client, to prevent the client from requesting this whenever they want. */
  struct nauka_toplevel *toplevel =
      wl_container_of(listener, toplevel, request_move);
  begin_interactive(toplevel, NAUKA_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(struct wl_listener *listener,
                                        void *data) {
  /* This event is raised when a client would like to begin an interactive
   * resize, typically because the user clicked on their client-side
   * decorations. Note that a more sophisticated compositor should check the
   * provided serial against a list of button press serials sent to this
   * client, to prevent the client from requesting this whenever they want. */
  struct wlr_xdg_toplevel_resize_event *event = data;
  struct nauka_toplevel *toplevel =
      wl_container_of(listener, toplevel, request_resize);
  begin_interactive(toplevel, NAUKA_CURSOR_RESIZE, event->edges);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener,
                                          void *data) {
  /* This event is raised when a client would like to maximize itself,
   * typically because the user clicked on the maximize button on client-side
   * decorations. nauka doesn't support maximization, but to conform to
   * xdg-shell protocol we still must send a configure.
   * wlr_xdg_surface_schedule_configure() is used to send an empty reply.
   * However, if the request was sent before an initial commit, we don't do
   * anything and let the client finish the initial surface setup. */
  struct nauka_toplevel *toplevel =
      wl_container_of(listener, toplevel, request_maximize);
  if (toplevel->xdg_toplevel->base->initialized) {
    wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
  }
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener,
                                            void *data) {
  /* Just as with request_maximize, we must send a configure here. */
  struct nauka_toplevel *toplevel =
      wl_container_of(listener, toplevel, request_fullscreen);
  if (toplevel->xdg_toplevel->base->initialized) {
    wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
  }
}

void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
  /* This event is raised when a client creates a new toplevel (application
   * window). */
  struct nauka_server *server =
      wl_container_of(listener, server, new_xdg_toplevel);
  struct wlr_xdg_toplevel *xdg_toplevel = data;

  /* Allocate a nauka_toplevel for this surface */
  struct nauka_toplevel *toplevel = calloc(1, sizeof(*toplevel));
  toplevel->server = server;
  toplevel->xdg_toplevel = xdg_toplevel;
  toplevel->scene_tree =
      wlr_scene_xdg_surface_create(server->toplevel_tree, xdg_toplevel->base);
  toplevel->scene_tree->node.data = toplevel;
  xdg_toplevel->base->data = toplevel;

  /*
   * Create the four border rectangles.
   *
   * RGBA values are floats in the range 0.0 - 1.0.
   * Initial color doesn't matter because focus will set it later.
   */
  static const float transparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};

  for (int i = 0; i < 4; i++) {
    toplevel->border[i] =
        wlr_scene_rect_create(toplevel->scene_tree, 0, 0, transparent);

    if (toplevel->border[i] == NULL) {
      /* Handle allocation failure appropriately in your compositor. */
      return;
    }
  }

  /* Listen to the various events it can emit */
  toplevel->map.notify = xdg_toplevel_map;
  wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
  toplevel->unmap.notify = xdg_toplevel_unmap;
  wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
  toplevel->commit.notify = xdg_toplevel_commit;
  wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

  toplevel->destroy.notify = xdg_toplevel_destroy;
  wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

  /* cotd */
  toplevel->request_move.notify = xdg_toplevel_request_move;
  wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
  toplevel->request_resize.notify = xdg_toplevel_request_resize;
  wl_signal_add(&xdg_toplevel->events.request_resize,
                &toplevel->request_resize);
  toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
  wl_signal_add(&xdg_toplevel->events.request_maximize,
                &toplevel->request_maximize);
  toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
  wl_signal_add(&xdg_toplevel->events.request_fullscreen,
                &toplevel->request_fullscreen);
}

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
  /* Called when a new surface state is committed. */
  struct nauka_popup *popup = wl_container_of(listener, popup, commit);

  if (popup->xdg_popup->base->initial_commit) {
    /* When an xdg_surface performs an initial commit, the compositor must
     * reply with a configure so the client can map the surface.
     * nauka sends an empty configure. A more sophisticated compositor
     * might change an xdg_popup's geometry to ensure it's not positioned
     * off-screen, for example. */
    wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
  }
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
  /* Called when the xdg_popup is destroyed. */
  struct nauka_popup *popup = wl_container_of(listener, popup, destroy);

  wl_list_remove(&popup->commit.link);
  wl_list_remove(&popup->destroy.link);

  free(popup);
}

void server_new_xdg_popup(struct wl_listener *listener, void *data) {
  /* This event is raised when a client creates a new popup. */
  struct wlr_xdg_popup *xdg_popup = data;

  if (xdg_popup->parent == NULL) {
    /* Layer-shell-anchored popup (e.g. a waybar tooltip). It has no
     * xdg_surface parent — it's associated via
     * wlr_layer_surface_v1.events.new_popup instead, handled in
     * layer_shell.c. Nothing to do here. */
    return;
  }

  struct nauka_popup *popup = calloc(1, sizeof(*popup));
  popup->xdg_popup = xdg_popup;

  /* We must add xdg popups to the scene graph so they get rendered. The
   * wlroots scene graph provides a helper for this, but to use it we must
   * provide the proper parent scene node of the xdg popup. To enable this,
   * we always set the user data field of xdg_surfaces to the corresponding
   * scene node. */
  struct wlr_scene_tree *parent_tree = NULL;
  struct wlr_xdg_surface *parent_xdg =
      wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);

  if (parent_xdg != NULL) {
    parent_tree = parent_xdg->data;
  } else {
    struct wlr_layer_surface_v1 *parent_layer =
        wlr_layer_surface_v1_try_from_wlr_surface(xdg_popup->parent);
    if (parent_layer != NULL) {
      parent_tree = parent_layer->data;
    }
  }

  if (parent_tree == NULL) {
    free(popup);
    return;
  }

  xdg_popup->base->data =
      wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

  popup->commit.notify = xdg_popup_commit;
  wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

  popup->destroy.notify = xdg_popup_destroy;
  wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}
