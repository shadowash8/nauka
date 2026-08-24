#define NAUKA_TAG_COUNT 9

struct nauka_server;

void tags_init(struct nauka_server *server);
void view_tag(struct nauka_server *server, int tag);
void move_focused_to_tag(struct nauka_server *server, int tag);
void workspace_manager_handle_commit(struct wl_listener *listener, void *data);
void workspace_manager_handle_destroy(struct wl_listener *listener, void *data);
void workspace_update_hidden(struct nauka_server *server, int tag);
