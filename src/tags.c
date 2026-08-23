#include "nauka.h"
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

void view_tag(struct nauka_server *server, int tag) {
  if (tag < 0 || tag >= NAUKA_TAG_COUNT || tag == server->current_tag) {
    return;
  }

  server->current_tag = tag;

  struct nauka_toplevel *toplevel;
  wl_list_for_each(toplevel, &server->toplevels, link) {
    bool visible = (toplevel->tag == tag);
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, visible);
  }

  wlr_seat_keyboard_clear_focus(server->seat);

  wl_list_for_each(toplevel, &server->toplevels, link) {
    if (toplevel->tag == tag) {
      focus_toplevel(toplevel);
      break;
    }
  }
  arrange_windows(server);
}

void move_focused_to_tag(struct nauka_server *server, int tag) {
  if (tag < 0 || tag >= NAUKA_TAG_COUNT) {
    return;
  }
  struct wlr_surface *surface = server->seat->keyboard_state.focused_surface;
  if (!surface)
    return;

  struct wlr_xdg_toplevel *xdg_toplevel =
      wlr_xdg_toplevel_try_from_wlr_surface(surface);
  if (!xdg_toplevel)
    return;
  struct nauka_toplevel *toplevel = xdg_toplevel->base->data;
  if (!toplevel)
    return;

  toplevel->tag = tag;
  wlr_scene_node_set_enabled(&toplevel->scene_tree->node,
                             tag == server->current_tag);

  if (tag != server->current_tag) {
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, false);
    toplevel_set_border_color(toplevel, false);

    if (server->focused_toplevel == toplevel) {
      server->focused_toplevel = NULL;
    }
    if (server->prev_focused == toplevel) {
      server->prev_focused = NULL;
    }

    wlr_seat_keyboard_clear_focus(server->seat);

    struct nauka_toplevel *it;
    wl_list_for_each(it, &server->toplevels, link) {
      if (it != toplevel && it->tag == server->current_tag) {
        focus_toplevel(it);
        break;
      }
    }
  }

  arrange_windows(server);
}
