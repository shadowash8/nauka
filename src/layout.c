#include "layout.h"
#include "nauka.h"

#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

void arrange_windows(struct nauka_server *server) {
  struct wlr_box area = {0};
  struct nauka_output *output =
      wl_container_of(server->outputs.next, output, link);
  wlr_output_layout_get_box(server->output_layout, output->wlr_output, &area);

  int outer = server->config.outer_gap;
  int inner = server->config.inner_gap;
  area.x += outer;
  area.y += outer;
  area.width -= outer * 2;
  area.height -= outer * 2;

  int n = 0;
  struct nauka_toplevel *t;
  wl_list_for_each(t, &server->toplevels, link) {
    if (t->tag == server->current_tag)
      n++;
  }
  if (n == 0)
    return;

  /* Compute a roughly square grid: cols x rows >= n */
  int cols = (int)ceil(sqrt((double)n));
  int rows = (int)ceil((double)n / cols);

  int cell_h = (area.height - inner * (rows - 1)) / rows;

  int i = 0;
  wl_list_for_each(t, &server->toplevels, link) {
    if (t->tag != server->current_tag)
      continue;

    int col = i % cols;
    int row = i / cols;

    /* Last row may have fewer windows than `cols`; stretch them to fill
     * the row instead of leaving a gap on the right. */
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
