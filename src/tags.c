#include "nauka.h"

#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_ext_workspace_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

void tags_init(struct nauka_server *server) {
  server->current_tag = 0;

  server->workspace_manager =
      wlr_ext_workspace_manager_v1_create(server->wl_display, 1);

  if (!server->workspace_manager) {
    fprintf(stderr, "nauka: failed to create workspace manager\n");
    return;
  }

  server->workspace_group =
      wlr_ext_workspace_group_handle_v1_create(server->workspace_manager, 0);

  if (!server->workspace_group) {
    fprintf(stderr, "nauka: failed to create workspace group\n");
    return;
  }

  for (int i = 0; i < NAUKA_TAG_COUNT; i++) {
    char id[16];
    snprintf(id, sizeof(id), "%d", i + 1);

    server->workspaces[i] = wlr_ext_workspace_handle_v1_create(
        server->workspace_manager, id,
        EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE);

    if (!server->workspaces[i]) {
      fprintf(stderr, "nauka: failed to create workspace %s\n", id);
      continue;
    }

    wlr_ext_workspace_handle_v1_set_name(server->workspaces[i], id);

    wlr_ext_workspace_handle_v1_set_group(server->workspaces[i],
                                          server->workspace_group);

    if (i == server->current_tag) {
      wlr_ext_workspace_handle_v1_set_active(server->workspaces[i], true);
      wlr_ext_workspace_handle_v1_set_hidden(server->workspaces[i], false);
    } else {
      wlr_ext_workspace_handle_v1_set_hidden(server->workspaces[i], true);
    }
  }

  server->workspace_manager_commit.notify = workspace_manager_handle_commit;

  wl_signal_add(&server->workspace_manager->events.commit,
                &server->workspace_manager_commit);

  server->workspace_manager_destroy.notify = workspace_manager_handle_destroy;

  wl_signal_add(&server->workspace_manager->events.destroy,
                &server->workspace_manager_destroy);
}

void view_tag(struct nauka_server *server, int tag) {
  if (tag < 0 || tag >= NAUKA_TAG_COUNT)
    return;

  if (tag == server->current_tag)
    return;

  int old_tag = server->current_tag;
  server->current_tag = tag;

  /* Update ext-workspace-v1 active state. */
  if (server->workspaces[old_tag] != NULL) {
    wlr_ext_workspace_handle_v1_set_active(server->workspaces[old_tag], false);
  }

  if (server->workspaces[tag] != NULL) {
    wlr_ext_workspace_handle_v1_set_active(server->workspaces[tag], true);
  }

  /* Update hidden state now that current_tag is correct. */
  workspace_update_hidden(server, old_tag);
  workspace_update_hidden(server, tag);

  /* Hide old tag and show new tag. */
  struct nauka_toplevel *toplevel;

  wl_list_for_each(toplevel, &server->toplevels, link) {
    bool visible = toplevel->sticky || toplevel->tag == tag;
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, visible);
  }

  wlr_seat_keyboard_clear_focus(server->seat);

  wl_list_for_each(toplevel, &server->toplevels, link) {
    if (toplevel->tag == tag || toplevel->sticky) {
      focus_toplevel(toplevel);
      break;
    }
  }

  arrange_windows(server);
}

void move_focused_to_tag(struct nauka_server *server, int tag) {
  if (tag < 0 || tag >= NAUKA_TAG_COUNT)
    return;

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

  int old_tag = toplevel->tag;
  toplevel->tag = tag;

  workspace_update_hidden(server, old_tag);
  workspace_update_hidden(server, tag);

  bool visible = toplevel->sticky || tag == server->current_tag;

  wlr_scene_node_set_enabled(&toplevel->scene_tree->node, visible);

  if (!visible) {
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, false);

    toplevel_set_border_color(toplevel, false);

    if (server->focused_toplevel == toplevel)
      server->focused_toplevel = NULL;

    if (server->prev_focused == toplevel)
      server->prev_focused = NULL;

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

void workspace_manager_handle_commit(struct wl_listener *listener, void *data) {

  struct nauka_server *server =
      wl_container_of(listener, server, workspace_manager_commit);

  struct wlr_ext_workspace_v1_commit_event *event = data;

  struct wlr_ext_workspace_v1_request *request;

  wl_list_for_each(request, event->requests, link) {
    switch (request->type) {

    case WLR_EXT_WORKSPACE_V1_REQUEST_ACTIVATE: {
      struct wlr_ext_workspace_handle_v1 *workspace =
          request->activate.workspace;

      if (!workspace)
        break;

      for (int i = 0; i < NAUKA_TAG_COUNT; i++) {
        if (server->workspaces[i] == workspace) {
          view_tag(server, i);
          break;
        }
      }

      break;
    }

    default:
      /*
       * We don't support clients creating/removing/
       * assigning workspaces. Nauka has fixed tags.
       */
      break;
    }
  }
}

void workspace_manager_handle_destroy(struct wl_listener *listener,
                                      void *data) {

  (void)data;

  struct nauka_server *server =
      wl_container_of(listener, server, workspace_manager_destroy);

  wl_list_remove(&server->workspace_manager_commit.link);
  wl_list_remove(&server->workspace_manager_destroy.link);
}

void workspace_update_hidden(struct nauka_server *server, int tag) {
  if (tag < 0 || tag >= NAUKA_TAG_COUNT || server->workspaces[tag] == NULL)
    return;

  bool has_toplevels = false;

  struct nauka_toplevel *toplevel;
  wl_list_for_each(toplevel, &server->toplevels, link) {
    if (toplevel->tag == tag) {
      has_toplevels = true;
      break;
    }
  }

  bool active = (server->current_tag == tag);

  wlr_ext_workspace_handle_v1_set_hidden(server->workspaces[tag],
                                         !active && !has_toplevels);
}
