#include "nauka.h"

#include <scenefx/types/wlr_scene.h>
#include <stdlib.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>

struct apply_radii_args {
  int32_t root_x;
  int32_t root_y;
  int width;
  int height;
  int radius;
};

struct nauka_buffer_blur {
  struct wlr_scene_blur *blur;
  struct wl_listener destroy;
};

static void iter_buffer_set_corner_radii(struct wlr_scene_buffer *buffer,
                                         int lx, int ly, void *data) {
  struct apply_radii_args *args = data;

  struct wlr_scene_surface *scene_surface =
      wlr_scene_surface_try_from_buffer(buffer);
  if (scene_surface == NULL) {
    return;
  }
  struct wlr_surface *surface = scene_surface->surface;

  /* don't round popups -- they shouldn't be clipped to the parent window */
  if (wlr_xdg_popup_try_from_wlr_surface(surface) != NULL) {
    return;
  }

  int32_t x = lx - args->root_x;
  int32_t y = ly - args->root_y;

  struct fx_corner_radii corners = corner_radii_none();

  if (x == 0 && y == 0) {
    corners.top_left = args->radius;
  }
  if (x == 0 && y + (int)surface->current.height == args->height) {
    corners.bottom_left = args->radius;
  }
  if (x + (int)surface->current.width == args->width && y == 0) {
    corners.top_right = args->radius;
  }
  if (x + (int)surface->current.width == args->width &&
      y + (int)surface->current.height == args->height) {
    corners.bottom_right = args->radius;
  }

  wlr_scene_buffer_set_corner_radii(buffer, corners);
}

static void toplevel_apply_content_radii(struct nauka_toplevel *toplevel,
                                         int width, int height, int radius) {
  struct apply_radii_args args = {
      .root_x = toplevel->scene_tree->node.x,
      .root_y = toplevel->scene_tree->node.y,
      .width = width,
      .height = height,
      .radius = radius,
  };
  wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node,
                                 iter_buffer_set_corner_radii, &args);
}

void toplevel_update_borders(struct nauka_toplevel *toplevel) {
  struct nauka_config *config = &toplevel->server->config;

  struct wlr_box geo = toplevel->xdg_toplevel->base->geometry;
  int w = geo.width;
  int h = geo.height;
  int bw = config->border_width;
  int r = toplevel->corner_radius;

  if (toplevel->is_fullscreen) {
    wlr_scene_node_set_enabled(&toplevel->border_tree->node, false);
    toplevel_apply_content_radii(toplevel, w, h, 0);
    return;
  }

  if (toplevel->border == NULL) {
    toplevel->border = wlr_scene_rect_create(toplevel->border_tree, 0, 0,
                                             config->border_color_inactive);
  }

  /* outer rect covers window + border on all sides */
  wlr_scene_node_set_position(&toplevel->border->node, -bw, -bw);
  wlr_scene_rect_set_size(toplevel->border, w + 2 * bw, h + 2 * bw);

  struct fx_corner_radii outer_radii = corner_radii_new(r, r, r, r);
  wlr_scene_rect_set_corner_radii(toplevel->border, outer_radii);

  /* punch a hole the size of the window out of the middle, so what's left
   * is a rounded ring of thickness bw rather than a filled rect */
  int inner_r = r - bw;
  if (inner_r < 0) {
    inner_r = 0;
  }
  struct clipped_region clipped_region = {
      .area = {bw, bw, w, h},
      .corners = corner_radii_new(inner_r, inner_r, inner_r, inner_r),
  };
  wlr_scene_rect_set_clipped_region(toplevel->border, clipped_region);

  /* round the client's own content to match, so it doesn't poke out past
   * the rounded corners of the border */
  toplevel_apply_content_radii(toplevel, w, h, inner_r);
}

void toplevel_set_border_color(struct nauka_toplevel *toplevel, bool active) {
  struct nauka_config *config = &toplevel->server->config;
  const float *color =
      active ? config->border_color_active : config->border_color_inactive;
  if (toplevel->border != NULL) {
    wlr_scene_rect_set_color(toplevel->border, color);
  }
}

static void buffer_blur_handle_destroy(struct wl_listener *listener,
                                       void *data) {
  struct nauka_buffer_blur *bb = wl_container_of(listener, bb, destroy);
  wl_list_remove(&bb->destroy.link);
  free(bb);
}

static struct wlr_scene_blur *
buffer_ensure_blur(struct wlr_scene_buffer *buffer) {
  struct nauka_buffer_blur *bb = buffer->node.data;
  if (bb != NULL) {
    return bb->blur;
  }

  bb = calloc(1, sizeof(*bb));
  bb->blur = wlr_scene_blur_create(buffer->node.parent, 1, 1);
  wlr_scene_node_place_below(&bb->blur->node, &buffer->node);
  wlr_scene_node_set_enabled(&bb->blur->node, false);

  bb->destroy.notify = buffer_blur_handle_destroy;
  wl_signal_add(&buffer->node.events.destroy, &bb->destroy);

  buffer->node.data = bb;
  return bb->blur;
}

static struct fx_corner_radii compute_edge_radii(int32_t root_x, int32_t root_y,
                                                 int lx, int ly, int surf_w,
                                                 int surf_h, int width,
                                                 int height, int radius) {
  int32_t x = lx - root_x;
  int32_t y = ly - root_y;

  struct fx_corner_radii corners = corner_radii_none();
  if (x == 0 && y == 0) {
    corners.top_left = radius;
  }
  if (x == 0 && y + surf_h == height) {
    corners.bottom_left = radius;
  }
  if (x + surf_w == width && y == 0) {
    corners.top_right = radius;
  }
  if (x + surf_w == width && y + surf_h == height) {
    corners.bottom_right = radius;
  }
  return corners;
}

static void iter_buffer_apply_effects(struct wlr_scene_buffer *buffer, int lx,
                                      int ly, void *data) {
  struct nauka_toplevel *toplevel = data;
  struct nauka_config *config = &toplevel->server->config;

  /* opacity applies to every buffer under this toplevel, popups included */
  wlr_scene_buffer_set_opacity(buffer, toplevel->opacity);

  struct wlr_scene_surface *scene_surface =
      wlr_scene_surface_try_from_buffer(buffer);
  if (scene_surface == NULL) {
    return;
  }
  struct wlr_surface *surface = scene_surface->surface;

  /* don't blur popups or subsurfaces -- only the toplevel's own main buffer */
  if (wlr_xdg_popup_try_from_wlr_surface(surface) != NULL) {
    return;
  }
  if (wlr_subsurface_try_from_wlr_surface(surface) != NULL) {
    return;
  }

  struct wlr_scene_blur *blur = buffer_ensure_blur(buffer);

  if (!config->blur) {
    wlr_scene_node_set_enabled(&blur->node, false);
    return;
  }

  wlr_scene_blur_set_size(blur, surface->current.width,
                          surface->current.height);
  wlr_scene_node_set_position(&blur->node, buffer->node.x, buffer->node.y);
  wlr_scene_blur_set_strength(blur, config->blur_strength);
  wlr_scene_blur_set_alpha(blur, config->blur_alpha);

  /* clip the blur to match the windows's rounded corners */
  struct wlr_box geo = toplevel->xdg_toplevel->base->geometry;
  int bw = config->border_width;
  int inner_r = toplevel->is_fullscreen ? 0 : toplevel->corner_radius - bw;
  if (inner_r < 0)
    inner_r = 0;
  struct fx_corner_radii corners = compute_edge_radii(
      toplevel->scene_tree->node.x, toplevel->scene_tree->node.y, lx, ly,
      surface->current.width, surface->current.height, geo.width, geo.height,
      inner_r);
  wlr_scene_blur_set_corner_radii(blur, corners);

  wlr_scene_node_set_enabled(&blur->node, true);
}

void toplevel_update_opacity(struct nauka_toplevel *toplevel, bool active) {
  struct nauka_config *config = &toplevel->server->config;
  toplevel->opacity =
      active ? config->opacity_active : config->opacity_inactive;
  wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node,
                                 iter_buffer_apply_effects, toplevel);
}

void toplevel_update_blur(struct nauka_toplevel *toplevel) {
  wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node,
                                 iter_buffer_apply_effects, toplevel);
}
