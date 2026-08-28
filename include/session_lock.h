#include <wayland-server-core.h>

struct nauka_server;
struct nauka_output;

struct nauka_session_lock {
  struct nauka_server *server;
  struct wlr_session_lock_v1 *wlr_lock;

  struct wl_list surfaces;

  struct wl_listener new_surface;
  struct wl_listener unlock;
  struct wl_listener destroy;
};

struct nauka_session_lock_surface {
  struct nauka_session_lock *lock;
  struct nauka_output *output;
  struct wlr_session_lock_surface_v1 *wlr_lock_surface;
  struct wlr_scene_tree *scene_tree;

  struct wl_listener destroy;
  struct wl_listener map;
  struct wl_listener output_commit;

  struct wl_list link;
};

void server_new_session_lock(struct wl_listener *listener, void *data);

void session_lock_create_output_state(struct nauka_output *output);

void session_lock_update_output(struct nauka_output *output);

void session_lock_output_destroy(struct nauka_output *output);
