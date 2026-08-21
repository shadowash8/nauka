#include "config.h"
#include "nauka.h"
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <wayland-server-core.h>

#include <wlr/backend/session.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <xkbcommon/xkbcommon.h>

void seat_focus_surface(struct nauka_server *server,
                        struct wlr_surface *surface) {
  struct wlr_seat *seat = server->seat;
  struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
  if (prev_surface == surface) {
    return;
  }
  if (prev_surface) {
    struct wlr_xdg_toplevel *prev_toplevel =
        wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
    if (prev_toplevel != NULL) {
      wlr_xdg_toplevel_set_activated(prev_toplevel, false);
    }
  }
  struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
  if (keyboard != NULL) {
    wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
                                   keyboard->num_keycodes,
                                   &keyboard->modifiers);
  }
}

void focus_toplevel(struct nauka_toplevel *toplevel) {
  if (toplevel == NULL) {
    return;
  }
  struct nauka_server *server = toplevel->server;
  struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;

  wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
  wl_list_remove(&toplevel->link);
  wl_list_insert(&server->toplevels, &toplevel->link);

  wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
  seat_focus_surface(server, surface);
}

static void keyboard_handle_modifiers(struct wl_listener *listener,
                                      void *data) {
  (void)data;

  /* This event is raised when a modifier key, such as shift or alt, is
   * pressed. We simply communicate this to the client. */
  struct nauka_keyboard *keyboard =
      wl_container_of(listener, keyboard, modifiers);
  /*
   * A seat can only have one keyboard, but this is a limitation of the
   * Wayland protocol - not wlroots. We assign all connected keyboards to the
   * same seat. You can swap out the underlying wlr_keyboard like this and
   * wlr_seat handles this transparently.
   */
  wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
  /* Send modifiers to the client. */
  wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
                                     &keyboard->wlr_keyboard->modifiers);
}

static bool try_keybindings(struct nauka_server *server, uint32_t modifiers,
                            const xkb_keysym_t *syms, int nsyms) {
  bool handled = false;

  for (struct nauka_keybind *kb = server->config.keybinds; kb != NULL;
       kb = kb->next) {
    uint32_t mask = WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT |
                    WLR_MODIFIER_LOGO;

    if ((modifiers & mask) != kb->mods) {
      continue;
    }
    for (int i = 0; i < nsyms; i++) {
      if (xkb_keysym_to_lower(syms[i]) != xkb_keysym_to_lower(kb->keysym)) {
        continue;
      }

      switch (kb->action) {
      case NAUKA_ACTION_RUN: {
        pid_t pid = fork();
        if (pid == 0) {
          execl("/bin/sh", "/bin/sh", "-c", kb->command, (void *)NULL);
          _exit(1);
        }
        break;
      }
      case NAUKA_ACTION_EXIT:
        wl_display_terminate(server->wl_display);
        break;
      case NAUKA_ACTION_CLOSE_ACTIVE: {
        struct wlr_surface *surface =
            server->seat->keyboard_state.focused_surface;
        if (surface) {
          struct wlr_xdg_toplevel *toplevel =
              wlr_xdg_toplevel_try_from_wlr_surface(surface);
          if (toplevel) {
            wlr_xdg_toplevel_send_close(toplevel);
          }
        }
        break;
      }
      case NAUKA_ACTION_NEXT_TOPLEVEL:
        if (wl_list_length(&server->toplevels) >= 2) {
          struct nauka_toplevel *next_toplevel =
              wl_container_of(server->toplevels.prev, next_toplevel, link);
          focus_toplevel(next_toplevel);
        }
        break;
      }

      handled = true;
    }
  }

  return handled;
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
  struct nauka_keyboard *keyboard = wl_container_of(listener, keyboard, key);
  struct nauka_server *server = keyboard->server;
  struct wlr_keyboard_key_event *event = data;
  struct wlr_seat *seat = server->seat;

  uint32_t keycode = event->keycode + 8;
  const xkb_keysym_t *syms;
  int nsyms =
      xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

  if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    uint32_t mods = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

    if ((mods & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT)) ==
        (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT)) {

      xkb_keysym_t sym = xkb_keysym_to_lower(syms[0]);

      if (sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12) {

        unsigned vt = sym - XKB_KEY_XF86Switch_VT_1 + 1;
        wlr_session_change_vt(server->session, vt);
        return;
      }
    }
  }

  bool handled = false;
  uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
  if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    handled = try_keybindings(server, modifiers, syms, nsyms);
  }

  if (!handled) {
    wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode,
                                 event->state);
  }
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
  (void)data;

  /* This event is raised by the keyboard base wlr_input_device to signal
   * the destruction of the wlr_keyboard. It will no longer receive events
   * and should be destroyed.
   */
  struct nauka_keyboard *keyboard =
      wl_container_of(listener, keyboard, destroy);
  wl_list_remove(&keyboard->modifiers.link);
  wl_list_remove(&keyboard->key.link);
  wl_list_remove(&keyboard->destroy.link);
  wl_list_remove(&keyboard->link);
  free(keyboard);
}

void server_new_keyboard(struct nauka_server *server,
                         struct wlr_input_device *device) {
  struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

  struct nauka_keyboard *keyboard = calloc(1, sizeof(*keyboard));
  keyboard->server = server;
  keyboard->wlr_keyboard = wlr_keyboard;

  /* We need to prepare an XKB keymap and assign it to the keyboard. This
   * assumes the defaults (e.g. layout = "us"). */
  struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  struct xkb_keymap *keymap =
      xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

  wlr_keyboard_set_keymap(wlr_keyboard, keymap);
  xkb_keymap_unref(keymap);
  xkb_context_unref(context);
  wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

  /* Here we set up listeners for keyboard events. */
  keyboard->modifiers.notify = keyboard_handle_modifiers;
  wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
  keyboard->key.notify = keyboard_handle_key;
  wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
  keyboard->destroy.notify = keyboard_handle_destroy;
  wl_signal_add(&device->events.destroy, &keyboard->destroy);

  wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

  /* And add the keyboard to our list of keyboards */
  wl_list_insert(&server->keyboards, &keyboard->link);
}
