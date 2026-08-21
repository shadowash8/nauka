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

  if (n == 1) {
    wl_list_for_each(t, &server->toplevels, link) {
      if (t->tag != server->current_tag)
        continue;

      wlr_scene_node_set_position(&t->scene_tree->node, area.x, area.y);
      wlr_xdg_toplevel_set_size(t->xdg_toplevel, area.width, area.height);
    }
    return;
  }

  int master_w = (area.width - inner) * 3 / 5;
  int stack_w = area.width - master_w - inner;
  int stack_h = (area.height - inner * (n - 2)) / (n - 1);

  int i = 0;

  wl_list_for_each(t, &server->toplevels, link) {
    if (t->tag != server->current_tag)
      continue;

    if (i == 0) {
      wlr_scene_node_set_position(&t->scene_tree->node, area.x, area.y);

      wlr_xdg_toplevel_set_size(t->xdg_toplevel, master_w, area.height);
    } else {
      wlr_scene_node_set_position(&t->scene_tree->node,
                                  area.x + master_w + inner,
                                  area.y + (i - 1) * (stack_h + inner));

      wlr_xdg_toplevel_set_size(
          t->xdg_toplevel, stack_w,
          (i == n - 1) ? area.height - (stack_h + inner) * (n - 2) : stack_h);
    }

    i++;
  }
}
