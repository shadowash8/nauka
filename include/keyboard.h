#include "toplevel.h"
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_input_device.h>

struct nauka_keyboard {
  struct wl_list link;
  struct nauka_server *server;
  struct wlr_keyboard *wlr_keyboard;

  struct wl_listener modifiers;
  struct wl_listener key;
  struct wl_listener destroy;
};

void seat_focus_surface(struct nauka_server *server,
                        struct wlr_surface *surface);

void focus_toplevel(struct nauka_toplevel *toplevel);

void server_new_keyboard(struct nauka_server *server,
                         struct wlr_input_device *device);

void server_new_kb_shortcuts_inhibitor(struct wl_listener *listener,
                                       void *data);

bool focused_surface_has_active_inhibitor(struct nauka_server *server);
