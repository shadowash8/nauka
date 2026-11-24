#ifndef NAUKA_IPC_H
#define NAUKA_IPC_H

#include <stddef.h>

struct nauka_server;
struct nauka_toplevel;
struct nauka_output;
struct nauka_layer_surface;
struct nauka_ipc;

/* Path used by both the compositor and the nauka-ipc client. Honours
 * NAUKA_IPC_SOCKET, otherwise falls back to
 * $XDG_RUNTIME_DIR/nauka-ipc.sock. */
const char *nauka_ipc_socket_path(char *buf, size_t len);

void nauka_ipc_init(struct nauka_server *server);
void nauka_ipc_finish(struct nauka_server *server);

void nauka_ipc_toplevel_mapped(struct nauka_toplevel *toplevel);
void nauka_ipc_toplevel_unmapped(struct nauka_toplevel *toplevel);
void nauka_ipc_toplevel_updated(struct nauka_toplevel *toplevel);
void nauka_ipc_toplevel_focused(struct nauka_toplevel *toplevel);
void nauka_ipc_toplevel_tagged(struct nauka_toplevel *toplevel);
void nauka_ipc_toplevel_state(struct nauka_toplevel *toplevel);

void nauka_ipc_tag_changed(struct nauka_server *server);
void nauka_ipc_layout_changed(struct nauka_server *server);

void nauka_ipc_output_added(struct nauka_output *output);
void nauka_ipc_output_removed(struct nauka_output *output);

void nauka_ipc_layer_added(struct nauka_layer_surface *layer);
void nauka_ipc_layer_removed(struct nauka_layer_surface *layer);

#endif /* NAUKA_IPC_H */
