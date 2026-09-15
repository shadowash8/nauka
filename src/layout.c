#include "layout.h"
#include "nauka.h"

#include <math.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_shell.h>

static void arrange_grid(struct nauka_server *server, struct wlr_box area) {
  int inner = server->config.inner_gap;

  int n = 0;
  struct nauka_toplevel *t;
  wl_list_for_each(t, &server->toplevels, link) {
    if (t->floating || !toplevel_is_visible(t))
      continue;
    if (t->tag == server->current_tag)
      n++;
  }
  if (n == 0)
    return;

  int cols = (int)ceil(sqrt((double)n));
  int rows = (int)ceil((double)n / cols);
  int cell_h = (area.height - inner * (rows - 1)) / rows;
  int i = 0;
  wl_list_for_each(t, &server->toplevels, link) {
    if (t->tag != server->current_tag || t->floating)
      continue;
    int col = i % cols;
    int row = i / cols;
    int windows_in_row = (row == rows - 1) ? (n - row * cols) : cols;
    int this_cell_w =
        (area.width - inner * (windows_in_row - 1)) / windows_in_row;
    int x = area.x + col * (this_cell_w + inner);
    int y = area.y + row * (cell_h + inner);
    wlr_scene_node_set_position(&t->scene_tree->node, x, y);
    wlr_xdg_toplevel_set_size(t->xdg_toplevel, this_cell_w, cell_h);
    i++;
  }
}

static void arrange_master(struct nauka_server *server, struct wlr_box area) {
  int inner = server->config.inner_gap;

  int n = 0;
  struct nauka_toplevel *t;
  wl_list_for_each(t, &server->toplevels, link) {
    if (t->floating || !toplevel_is_visible(t))
      continue;
    if (t->tag == server->current_tag)
      n++;
  }
  if (n == 0)
    return;

  double mfact = server->config.master_factor;
  if (mfact <= 0.1 || mfact >= 0.9)
    mfact = 0.55;

  int master_w = (n == 1) ? area.width : (int)(area.width * mfact) - inner / 2;
  int stack_w = area.width - master_w - inner;
  int stack_count = n - 1;

  int i = 0;
  wl_list_for_each(t, &server->toplevels, link) {
    if (t->tag != server->current_tag || t->floating)
      continue;

    if (i == 0) {
      wlr_scene_node_set_position(&t->scene_tree->node, area.x, area.y);
      wlr_xdg_toplevel_set_size(t->xdg_toplevel, master_w, area.height);
    } else {
      int stack_idx = i - 1;
      int cell_h = (area.height - inner * (stack_count - 1)) / stack_count;
      int x = area.x + master_w + inner;
      int y = area.y + stack_idx * (cell_h + inner);
      wlr_scene_node_set_position(&t->scene_tree->node, x, y);
      wlr_xdg_toplevel_set_size(t->xdg_toplevel, stack_w, cell_h);
    }
    i++;
  }
}

void arrange_windows(struct nauka_server *server) {
  struct nauka_output *output;
  wl_list_for_each(output, &server->outputs, link) {
    struct wlr_box area = output->usable_area;
    int outer = server->config.outer_gap;
    area.x += outer;
    area.y += outer;
    area.width -= outer * 2;
    area.height -= outer * 2;

    /* fullscreen overrides whatever layout is active */
    struct nauka_toplevel *t;
    bool has_fullscreen = false;
    wl_list_for_each(t, &server->toplevels, link) {
      if (t->tag != server->current_tag && !t->sticky)
        continue;
      if (t->is_fullscreen) {
        struct wlr_box full = {0};
        wlr_output_layout_get_box(server->output_layout, output->wlr_output,
                                  &full);
        wlr_scene_node_set_position(&t->scene_tree->node, full.x, full.y);
        wlr_xdg_toplevel_set_size(t->xdg_toplevel, full.width, full.height);
        has_fullscreen = true;
      }
    }
    if (has_fullscreen)
      continue;

    switch (server->current_layout) {
    case NAUKA_LAYOUT_MASTER:
      arrange_master(server, area);
      break;
    case NAUKA_LAYOUT_GRID:
    default:
      arrange_grid(server, area);
      break;
    }
  }
}

void layout_set(struct nauka_server *server, enum nauka_layout_mode mode) {
  if (mode < 0 || mode >= NAUKA_LAYOUT_COUNT)
    return;
  server->current_layout = mode;
  arrange_windows(server);
}

void layout_cycle(struct nauka_server *server) {
  layout_set(server, (server->current_layout + 1) % NAUKA_LAYOUT_COUNT);
}
