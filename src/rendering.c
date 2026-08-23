#include "nauka.h"

#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h> // wlr_xdg_popup_try_from_wlr_surface

struct apply_radii_args {
  int32_t root_x;
  int32_t root_y;
  int width;
  int height;
  int radius;
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
  struct nauka_config *cfg = &toplevel->server->config;

  struct wlr_box geo = toplevel->xdg_toplevel->base->geometry;
  int w = geo.width;
  int h = geo.height;
  int bw = cfg->border_width;
  int r = toplevel->corner_radius;

  if (toplevel->border == NULL) {
    toplevel->border = wlr_scene_rect_create(toplevel->border_tree, 0, 0,
                                             cfg->border_color_inactive);
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
  struct nauka_config *cfg = &toplevel->server->config;
  const float *color =
      active ? cfg->border_color_active : cfg->border_color_inactive;
  if (toplevel->border != NULL) {
    wlr_scene_rect_set_color(toplevel->border, color);
  }
}

static void iter_buffer_set_opacity(struct wlr_scene_buffer *buffer, int lx,
                                    int ly, void *data) {
  float *opacity = data;
  wlr_scene_buffer_set_opacity(buffer, *opacity);
}

void toplevel_update_opacity(struct nauka_toplevel *toplevel, bool active) {
  struct nauka_config *cfg = &toplevel->server->config;
  toplevel->opacity = active ? cfg->opacity_active : cfg->opacity_inactive;
  wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node,
                                 iter_buffer_set_opacity, &toplevel->opacity);
}
