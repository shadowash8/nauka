#include <wlr/types/wlr_input_device.h>

enum nauka_cursor_mode {
  NAUKA_CURSOR_PASSTHROUGH,
  NAUKA_CURSOR_MOVE,
  NAUKA_CURSOR_RESIZE,
};

void seat_pointer_focus_change(struct wl_listener *listener, void *data);
void server_cursor_motion(struct wl_listener *listener, void *data);
void server_cursor_motion_absolute(struct wl_listener *listener, void *data);
void server_cursor_button(struct wl_listener *listener, void *data);
void server_cursor_axis(struct wl_listener *listener, void *data);
void server_cursor_frame(struct wl_listener *listener, void *data);

void server_new_input(struct wl_listener *listener, void *data);
void seat_request_cursor(struct wl_listener *listener, void *data);
void seat_request_set_selection(struct wl_listener *listener, void *data);

void reset_cursor_mode(struct nauka_server *server);
