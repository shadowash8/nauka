#include "nauka.h"
#include <assert.h>
#include <stdlib.h>

#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
  /* Called when the surface is mapped, or ready to display on-screen. */
  struct nauka_toplevel *toplevel = wl_container_of(listener, toplevel, map);

  /* Set toplevel tag to current focused tag */
  toplevel->tag = toplevel->server->current_tag;
  workspace_update_hidden(toplevel->server, toplevel->tag);

  struct wlr_box box = {0};
  struct wlr_output *output = wlr_output_layout_output_at(
      toplevel->server->output_layout, toplevel->server->cursor->x,
      toplevel->server->cursor->y);
  if (output != NULL) {
    wlr_output_layout_get_box(toplevel->server->output_layout, output, &box);
  }

  if (toplevel->floating) {
    wlr_scene_node_reparent(&toplevel->scene_tree->node,
                            toplevel->server->floating_tree);

    int width = box.width / 2;
    int height = box.height / 2;

    toplevel->floating_geometry = (struct wlr_box){
        .x = box.x + (box.width - width) / 2,
        .y = box.y + (box.height - height) / 2,
        .width = width,
        .height = height,
    };

    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                toplevel->floating_geometry.x,
                                toplevel->floating_geometry.y);

    /* actually request this size from the client — without this, only
     * the scene node moves, and the surface keeps whatever size it
     * initially committed */
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);

    wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
    toplevel_update_borders(toplevel);
    toplevel_update_blur(toplevel);
    toplevel_update_opacity(toplevel,
                            toplevel == toplevel->server->focused_toplevel);
    focus_toplevel(toplevel);
    /* deliberately skip arrange_windows() — floaters don't participate
     * in the tiling layout */
    return;
  }

  wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
  toplevel_update_borders(toplevel);
  toplevel_update_blur(toplevel);
  toplevel_update_opacity(toplevel,
                          toplevel == toplevel->server->focused_toplevel);
  focus_toplevel(toplevel);
  arrange_windows(toplevel->server);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
  /* Called when the surface is unmapped, and should no longer be shown. */
  struct nauka_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
  struct nauka_server *server = toplevel->server;

  /* Reset the cursor mode if the grabbed toplevel was unmapped. */
  if (toplevel == server->grabbed_toplevel) {
    reset_cursor_mode(server);
  }

  wl_list_remove(&toplevel->link);

  if (server->prev_focused == toplevel) {
    server->prev_focused = NULL;
  }

  if (server->focused_toplevel == toplevel) {
    server->focused_toplevel = NULL;

    struct nauka_toplevel *fallback = NULL;

    /* Prefer the previously focused toplevel, if it's still around and
     * on the current tag. */
    if (server->prev_focused != NULL &&
        (server->prev_focused->tag == server->current_tag ||
         server->prev_focused->sticky)) {
      fallback = server->prev_focused;
    }

    if (fallback == NULL) {
      struct nauka_toplevel *it;
      wl_list_for_each(it, &server->toplevels, link) {
        if (it->tag == server->current_tag || it->sticky) {
          fallback = it;
          break;
        }
      }
    }

    if (fallback != NULL) {
      focus_toplevel(fallback);
    }
  }

  arrange_windows(server);
}

static bool toplevel_should_float(struct nauka_toplevel *toplevel) {
  struct wlr_xdg_toplevel *t = toplevel->xdg_toplevel;
  bool fixed_size = t->current.max_width && t->current.max_height &&
                    t->current.max_width == t->current.min_width &&
                    t->current.max_height == t->current.min_height;
  return fixed_size || t->parent != NULL;
}

bool toplevel_is_visible(struct nauka_toplevel *toplevel) {
  return toplevel->sticky || toplevel->tag == toplevel->server->current_tag;
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
  /* Called when a new surface state is committed. */
  struct nauka_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

  if (toplevel->xdg_toplevel->base->initial_commit) {
    /* When an xdg_surface performs an initial commit, the compositor must
     * reply with a configure so the client can map the surface. nauka
     * configures the xdg_toplevel with 0,0 size to let the client pick the
     * dimensions itself. */
    toplevel->floating = toplevel_should_float(toplevel);
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
  }
  toplevel_update_borders(toplevel);
  toplevel_update_blur(toplevel);
  toplevel_update_opacity(toplevel,
                          toplevel == toplevel->server->focused_toplevel);
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

void update_toplevel_visibility(struct nauka_server *server) {
  struct nauka_toplevel *toplevel;

  wl_list_for_each(toplevel, &server->toplevels, link) {
    bool visible = toplevel->sticky || toplevel->tag == server->current_tag;
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, visible);
  }
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
  struct nauka_toplevel *toplevel =
      wl_container_of(listener, toplevel, request_move);

  if (toplevel->is_fullscreen)
    return;

  if (!toplevel->floating) {
    toplevel->floating = true;
    wlr_scene_node_reparent(&toplevel->scene_tree->node,
                            toplevel->server->floating_tree);
    arrange_windows(toplevel->server); /* re-flow remaining tiled windows */
  }

  begin_interactive(toplevel, NAUKA_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(struct wl_listener *listener,
                                        void *data) {
  struct wlr_xdg_toplevel_resize_event *event = data;
  struct nauka_toplevel *toplevel =
      wl_container_of(listener, toplevel, request_resize);

  if (!toplevel->floating || toplevel->is_fullscreen)
    return; /* tiled windows resize via arrange_windows, not drag */

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
  struct nauka_toplevel *t = wl_container_of(listener, t, request_fullscreen);
  toplevel_set_fullscreen(t, t->xdg_toplevel->requested.fullscreen);
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
  toplevel->corner_radius = server->config.border_radius;
  toplevel->border_tree = wlr_scene_tree_create(toplevel->scene_tree);
  wlr_scene_node_raise_to_top(&toplevel->border_tree->node);

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
    struct wlr_xdg_popup *parent_popup =
        wlr_xdg_popup_try_from_wlr_surface(xdg_popup->parent);

    if (parent_popup != NULL) {
      /* nested popup: parent's base->data is the wlr_scene_tree created
       * for that popup, not a nauka_toplevel */
      parent_tree = parent_xdg->data;
    } else {
      /* parent is a real toplevel */
      struct nauka_toplevel *parent_toplevel = parent_xdg->data;
      if (parent_toplevel != NULL) {
        parent_tree = parent_toplevel->scene_tree;
      }
    }
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

void toplevel_toggle_floating(struct nauka_toplevel *toplevel) {
  if (toplevel->sticky)
    return; /* must un-stick first */

  struct nauka_server *server = toplevel->server;
  toplevel->floating = !toplevel->floating;

  if (toplevel->floating) {
    wlr_scene_node_reparent(&toplevel->scene_tree->node, server->floating_tree);

    struct wlr_output *output = wlr_output_layout_output_at(
        server->output_layout, server->cursor->x, server->cursor->y);
    struct wlr_box box = {0};
    if (output != NULL) {
      wlr_output_layout_get_box(server->output_layout, output, &box);
    }

    int width = box.width / 2;
    int height = box.height / 2;

    toplevel->floating_geometry = (struct wlr_box){
        .x = box.x + (box.width - width) / 2,
        .y = box.y + (box.height - height) / 2,
        .width = width,
        .height = height,
    };

    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                toplevel->floating_geometry.x,
                                toplevel->floating_geometry.y);
    wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);
  } else {
    wlr_scene_node_reparent(&toplevel->scene_tree->node, server->toplevel_tree);
  }

  arrange_windows(server);
}

void toplevel_toggle_sticky(struct nauka_toplevel *toplevel) {
  if (toplevel == NULL || toplevel->is_fullscreen)
    return;

  struct nauka_server *server = toplevel->server;

  toplevel->sticky = !toplevel->sticky;

  if (toplevel->sticky) {
    /* becoming sticky: keep floating as before */
    if (!toplevel->floating) {
      toplevel->floating = true;
      wlr_scene_node_reparent(&toplevel->scene_tree->node,
                              server->floating_tree);
    }
  } else {
    int old_tag = toplevel->tag;
    toplevel->tag = server->current_tag;

    if (old_tag != toplevel->tag) {
      workspace_update_hidden(server, old_tag);
      workspace_update_hidden(server, toplevel->tag);
    }
  }

  update_toplevel_visibility(server);
  arrange_windows(server);
}

void toplevel_toggle_fullscreen(struct nauka_toplevel *toplevel) {
  if (toplevel == NULL)
    return;
  toplevel_set_fullscreen(toplevel, !toplevel->is_fullscreen);
}

void toplevel_apply_config(struct nauka_server *server) {
  struct nauka_toplevel *toplevel;
  wl_list_for_each(toplevel, &server->toplevels, link) {
    toplevel->corner_radius = server->config.border_radius;
    toplevel_update_borders(toplevel);
    toplevel_update_opacity(toplevel,
                            toplevel == toplevel->server->focused_toplevel);
  }
}

static void toplevel_save_floating_geometry(struct nauka_toplevel *toplevel) {
  struct wlr_box geo = toplevel->xdg_toplevel->base->geometry;
  toplevel->floating_geometry = (struct wlr_box){
      .x = toplevel->scene_tree->node.x,
      .y = toplevel->scene_tree->node.y,
      .width = geo.width,
      .height = geo.height,
  };
}

static void
toplevel_restore_floating_geometry(struct nauka_toplevel *toplevel) {
  wlr_scene_node_set_position(&toplevel->scene_tree->node,
                              toplevel->floating_geometry.x,
                              toplevel->floating_geometry.y);
  wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
                            toplevel->floating_geometry.width,
                            toplevel->floating_geometry.height);
}

void toplevel_set_fullscreen(struct nauka_toplevel *toplevel, bool fullscreen) {
  toplevel->is_fullscreen = fullscreen;
  wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, fullscreen);

  if (fullscreen) {
    if (toplevel->floating) {
      toplevel_save_floating_geometry(toplevel);
    }

    toplevel->sticky_before_fullscreen = toplevel->sticky;
    if (toplevel->sticky) {
      toplevel->tag_before_fullscreen = toplevel->tag;
      toplevel->sticky = false;
      toplevel->tag = toplevel->server->current_tag;
    }

    wlr_scene_node_reparent(&toplevel->scene_tree->node,
                            toplevel->server->fullscreen_tree);
  } else {
    struct wlr_scene_tree *dest = toplevel->floating
                                      ? toplevel->server->floating_tree
                                      : toplevel->server->toplevel_tree;
    wlr_scene_node_reparent(&toplevel->scene_tree->node, dest);

    if (toplevel->floating) {
      toplevel_restore_floating_geometry(toplevel);
    }

    if (toplevel->sticky_before_fullscreen) {
      int fullscreen_tag = toplevel->tag;
      toplevel->sticky = true;
      toplevel->sticky_before_fullscreen = false;
      toplevel->tag = toplevel->tag_before_fullscreen;
      toplevel->sticky_before_fullscreen = false;

      workspace_update_hidden(toplevel->server, fullscreen_tag);
      workspace_update_hidden(toplevel->server, toplevel->tag);
    }
  }

  wlr_scene_node_set_enabled(&toplevel->border_tree->node, !fullscreen);
  toplevel_update_blur(toplevel);

  update_toplevel_visibility(toplevel->server);
  arrange_windows(toplevel->server);
}
