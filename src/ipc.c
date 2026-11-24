#include "ipc.h"
#include "nauka.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#define IPC_LINE_BUF 4096
#define IPC_CLIENT_BUF 4096

struct nauka_ipc_client {
  struct wl_list link;
  struct nauka_server *server;
  int fd;
  struct wl_event_source *source;
  char buf[IPC_CLIENT_BUF];
  size_t len;
  bool subscribed;
};

struct nauka_ipc {
  struct nauka_server *server;
  int fd;
  char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
  struct wl_event_source *source;
  struct wl_list clients;
};

static const char *layout_name(enum nauka_layout_mode mode) {
  switch (mode) {
  case NAUKA_LAYOUT_GRID:
    return "grid";
  case NAUKA_LAYOUT_MASTER:
    return "master";
  case NAUKA_LAYOUT_SCROLLER:
    return "scroller";
  default:
    return "unknown";
  }
}

/* Keep one event per line: tabs separate fields, so flatten tabs/newlines
 * that may appear in client-provided strings. */
static void sanitize(char *dst, size_t dst_len, const char *src) {
  if (src == NULL)
    src = "";

  size_t i = 0;
  for (; src[i] != '\0' && i + 1 < dst_len; i++) {
    char c = src[i];
    dst[i] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
  }
  dst[i] = '\0';
}

static size_t format_line(char *buf, size_t size, const char *fmt, va_list ap) {
  if (size < 2)
    return 0;

  int n = vsnprintf(buf, size - 1, fmt, ap);
  if (n < 0)
    return 0;

  size_t len = (size_t)n;
  if (len > size - 2)
    len = size - 2;

  buf[len++] = '\n';
  buf[len] = '\0';
  return len;
}

static bool client_send(struct nauka_ipc_client *client, const char *data,
                        size_t len) {
  while (len > 0) {
    ssize_t n = send(client->fd, data, len, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return false;
    }
    if (n == 0)
      return false;
    data += n;
    len -= (size_t)n;
  }
  return true;
}

static bool client_sendf(struct nauka_ipc_client *client, const char *fmt,
                         ...) {
  char buf[IPC_LINE_BUF];
  va_list ap;
  va_start(ap, fmt);
  size_t len = format_line(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (len == 0)
    return false;

  return client_send(client, buf, len);
}

static void client_destroy(struct nauka_ipc_client *client) {
  wl_list_remove(&client->link);
  if (client->source != NULL)
    wl_event_source_remove(client->source);
  close(client->fd);
  free(client);
}

static void broadcast(struct nauka_server *server, const char *fmt, ...) {
  if (server->ipc == NULL)
    return;

  char buf[IPC_LINE_BUF];
  va_list ap;
  va_start(ap, fmt);
  size_t len = format_line(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (len == 0)
    return;

  struct nauka_ipc_client *client, *tmp;
  wl_list_for_each_safe(client, tmp, &server->ipc->clients, link) {
    if (!client->subscribed)
      continue;
    if (!client_send(client, buf, len))
      client_destroy(client);
  }
}

static void toplevel_fields(struct nauka_toplevel *toplevel, char *app_id,
                            size_t app_id_len, char *title, size_t title_len) {
  sanitize(app_id, app_id_len, toplevel->xdg_toplevel->app_id);
  sanitize(title, title_len, toplevel->xdg_toplevel->title);
}

static void toplevel_cache_fields(struct nauka_toplevel *toplevel,
                                  const char *app_id, const char *title) {
  snprintf(toplevel->ipc_app_id, sizeof(toplevel->ipc_app_id), "%s", app_id);
  snprintf(toplevel->ipc_title, sizeof(toplevel->ipc_title), "%s", title);
}

static void send_toplevel_new(struct nauka_ipc_client *client,
                              struct nauka_toplevel *toplevel) {
  char app_id[128], title[256];
  toplevel_fields(toplevel, app_id, sizeof(app_id), title, sizeof(title));
  client_sendf(client, "toplevel new %u %d %s\t%s", toplevel->ipc_id,
               toplevel->tag + 1, app_id, title);
}

static void send_toplevel_state(struct nauka_ipc_client *client,
                                struct nauka_toplevel *toplevel) {
  client_sendf(client, "toplevel state %u %d %d %d", toplevel->ipc_id,
               toplevel->floating, toplevel->is_fullscreen, toplevel->sticky);
}

static void send_toplevels(struct nauka_ipc_client *client) {
  struct nauka_toplevel *toplevel;
  wl_list_for_each(toplevel, &client->server->toplevels, link) {
    char app_id[128], title[256];
    toplevel_fields(toplevel, app_id, sizeof(app_id), title, sizeof(title));
    if (!client_sendf(client, "%s\t%s", app_id, title))
      return;
  }
}

static void send_layers(struct nauka_ipc_client *client) {
  struct nauka_output *output;
  wl_list_for_each(output, &client->server->outputs, link) {
    struct nauka_layer_surface *layer;
    wl_list_for_each(layer, &output->layers, link) {
      if (!layer->mapped)
        continue;

      char namespace[256];
      sanitize(namespace, sizeof(namespace), layer->layer_surface->namespace);
      if (!client_sendf(client, "%s", namespace))
        return;
    }
  }
}

static void send_outputs(struct nauka_ipc_client *client) {
  struct nauka_output *output;
  wl_list_for_each(output, &client->server->outputs, link) {
    if (!client_sendf(client, "%s", output->wlr_output->name))
      return;
  }
}

static void send_snapshot(struct nauka_ipc_client *client) {
  struct nauka_server *server = client->server;

  client_sendf(client, "tag %d", server->current_tag + 1);
  client_sendf(client, "layout %d %s", server->current_tag + 1,
               layout_name(server->tag_layout[server->current_tag]));

  struct nauka_toplevel *toplevel;
  wl_list_for_each(toplevel, &server->toplevels, link) {
    send_toplevel_new(client, toplevel);
    send_toplevel_state(client, toplevel);
  }

  struct nauka_output *output;
  wl_list_for_each(output, &server->outputs, link) {
    client_sendf(client, "output new %s", output->wlr_output->name);

    struct nauka_layer_surface *layer;
    wl_list_for_each(layer, &output->layers, link) {
      if (!layer->mapped)
        continue;

      char namespace[256];
      sanitize(namespace, sizeof(namespace), layer->layer_surface->namespace);
      client_sendf(client, "layer new %u %s %s", layer->ipc_id, namespace,
                   output->wlr_output->name);
    }
  }
}

static void handle_command(struct nauka_ipc_client *client, char *cmd) {
  while (*cmd == ' ' || *cmd == '\t')
    cmd++;

  char *end = cmd + strlen(cmd);
  while (end > cmd && (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
    *--end = '\0';

  if (strcmp(cmd, "subscribe") == 0) {
    client->subscribed = true;
    if (!client_sendf(client, "subscribed")) {
      client_destroy(client);
      return;
    }
    send_snapshot(client);
    return;
  }

  if (strcmp(cmd, "toplevels") == 0) {
    send_toplevels(client);
  } else if (strcmp(cmd, "layers") == 0) {
    send_layers(client);
  } else if (strcmp(cmd, "outputs") == 0) {
    send_outputs(client);
  } else {
    client_sendf(client, "error: unknown message '%s'", cmd);
  }

  client_destroy(client);
}

static int handle_client_readable(int fd, uint32_t mask, void *data) {
  struct nauka_ipc_client *client = data;

  if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
    client_destroy(client);
    return 0;
  }
  if (!(mask & WL_EVENT_READABLE))
    return 0;

  ssize_t n = recv(fd, client->buf + client->len,
                   sizeof(client->buf) - client->len - 1, 0);
  if (n < 0) {
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
      return 0;
    client_destroy(client);
    return 0;
  }
  if (n == 0) {
    client_destroy(client);
    return 0;
  }

  client->len += (size_t)n;
  client->buf[client->len] = '\0';

  char *newline = memchr(client->buf, '\n', client->len);
  if (newline == NULL) {
    if (client->len + 1 >= sizeof(client->buf)) {
      client_sendf(client, "error: message too long");
      client_destroy(client);
    }
    return 0;
  }

  *newline = '\0';
  handle_command(client, client->buf);
  return 0;
}

static int handle_accept(int fd, uint32_t mask, void *data) {
  struct nauka_server *server = data;

  if (!(mask & WL_EVENT_READABLE))
    return 0;

  for (;;) {
    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      if (errno == EINTR)
        continue;
      wlr_log(WLR_ERROR, "nauka-ipc: accept failed: %s", strerror(errno));
      break;
    }

    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags >= 0)
      fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    fcntl(client_fd, F_SETFD, FD_CLOEXEC);

    struct nauka_ipc_client *client = calloc(1, sizeof(*client));
    if (client == NULL) {
      close(client_fd);
      continue;
    }

    client->server = server;
    client->fd = client_fd;
    wl_list_insert(&server->ipc->clients, &client->link);

    client->source = wl_event_loop_add_fd(
        wl_display_get_event_loop(server->wl_display), client_fd,
        WL_EVENT_READABLE, handle_client_readable, client);
    if (client->source == NULL)
      client_destroy(client);
  }

  return 0;
}

void nauka_ipc_init(struct nauka_server *server) {
  if (server->ipc != NULL)
    return;

  struct nauka_ipc *ipc = calloc(1, sizeof(*ipc));
  if (ipc == NULL)
    return;

  ipc->server = server;
  wl_list_init(&ipc->clients);
  nauka_ipc_socket_path(ipc->path, sizeof(ipc->path));

  /* Don't steal a socket from another running compositor. */
  int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (probe >= 0) {
    struct sockaddr_un probe_addr = {.sun_family = AF_UNIX};
    snprintf(probe_addr.sun_path, sizeof(probe_addr.sun_path), "%s", ipc->path);
    bool live =
        connect(probe, (struct sockaddr *)&probe_addr, sizeof(probe_addr)) == 0;
    close(probe);
    if (live) {
      wlr_log(WLR_ERROR,
              "nauka-ipc: another instance is already listening on %s",
              ipc->path);
      free(ipc);
      return;
    }
  }

  unlink(ipc->path);

  ipc->fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (ipc->fd < 0) {
    wlr_log(WLR_ERROR, "nauka-ipc: socket failed: %s", strerror(errno));
    free(ipc);
    return;
  }

  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", ipc->path);

  if (bind(ipc->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    wlr_log(WLR_ERROR, "nauka-ipc: bind %s failed: %s", ipc->path,
            strerror(errno));
    close(ipc->fd);
    free(ipc);
    return;
  }

  chmod(ipc->path, 0600);

  if (listen(ipc->fd, 16) < 0) {
    wlr_log(WLR_ERROR, "nauka-ipc: listen failed: %s", strerror(errno));
    close(ipc->fd);
    unlink(ipc->path);
    free(ipc);
    return;
  }

  int flags = fcntl(ipc->fd, F_GETFL, 0);
  if (flags >= 0)
    fcntl(ipc->fd, F_SETFL, flags | O_NONBLOCK);

  ipc->source =
      wl_event_loop_add_fd(wl_display_get_event_loop(server->wl_display),
                           ipc->fd, WL_EVENT_READABLE, handle_accept, server);
  if (ipc->source == NULL) {
    wlr_log(WLR_ERROR, "nauka-ipc: failed to add event source");
    close(ipc->fd);
    unlink(ipc->path);
    free(ipc);
    return;
  }

  server->ipc = ipc;
  wlr_log(WLR_INFO, "nauka-ipc: listening on %s", ipc->path);
}

void nauka_ipc_finish(struct nauka_server *server) {
  struct nauka_ipc *ipc = server->ipc;
  if (ipc == NULL)
    return;

  struct nauka_ipc_client *client, *tmp;
  wl_list_for_each_safe(client, tmp, &ipc->clients, link)
      client_destroy(client);

  wl_event_source_remove(ipc->source);
  close(ipc->fd);
  unlink(ipc->path);
  free(ipc);
  server->ipc = NULL;
}

void nauka_ipc_toplevel_mapped(struct nauka_toplevel *toplevel) {
  struct nauka_server *server = toplevel->server;

  toplevel->ipc_id = ++server->next_ipc_id;

  char app_id[128], title[256];
  toplevel_fields(toplevel, app_id, sizeof(app_id), title, sizeof(title));
  toplevel_cache_fields(toplevel, app_id, title);

  broadcast(server, "toplevel new %u %d %s\t%s", toplevel->ipc_id,
            toplevel->tag + 1, app_id, title);
  nauka_ipc_toplevel_state(toplevel);
}

void nauka_ipc_toplevel_unmapped(struct nauka_toplevel *toplevel) {
  if (toplevel->ipc_id == 0)
    return;

  broadcast(toplevel->server, "toplevel destroy %u", toplevel->ipc_id);
  toplevel->ipc_id = 0;
}

void nauka_ipc_toplevel_updated(struct nauka_toplevel *toplevel) {
  if (toplevel->ipc_id == 0)
    return;

  char app_id[128], title[256];
  toplevel_fields(toplevel, app_id, sizeof(app_id), title, sizeof(title));

  if (strcmp(app_id, toplevel->ipc_app_id) == 0 &&
      strcmp(title, toplevel->ipc_title) == 0)
    return;

  toplevel_cache_fields(toplevel, app_id, title);
  broadcast(toplevel->server, "toplevel title %u %s\t%s", toplevel->ipc_id,
            app_id, title);
}

void nauka_ipc_toplevel_focused(struct nauka_toplevel *toplevel) {
  if (toplevel->ipc_id == 0)
    return;
  broadcast(toplevel->server, "toplevel focus %u %s\t%s", toplevel->ipc_id,
            toplevel->ipc_app_id, toplevel->ipc_title);
}

void nauka_ipc_toplevel_tagged(struct nauka_toplevel *toplevel) {
  if (toplevel->ipc_id == 0)
    return;
  broadcast(toplevel->server, "toplevel tag %u %d", toplevel->ipc_id,
            toplevel->tag + 1);
}

void nauka_ipc_toplevel_state(struct nauka_toplevel *toplevel) {
  if (toplevel->ipc_id == 0)
    return;
  broadcast(toplevel->server, "toplevel state %u %d %d %d", toplevel->ipc_id,
            toplevel->floating, toplevel->is_fullscreen, toplevel->sticky);
}

void nauka_ipc_tag_changed(struct nauka_server *server) {
  broadcast(server, "tag %d", server->current_tag + 1);
  broadcast(server, "layout %d %s", server->current_tag + 1,
            layout_name(server->tag_layout[server->current_tag]));
}

void nauka_ipc_layout_changed(struct nauka_server *server) {
  broadcast(server, "layout %d %s", server->current_tag + 1,
            layout_name(server->tag_layout[server->current_tag]));
}

void nauka_ipc_output_added(struct nauka_output *output) {
  broadcast(output->server, "output new %s", output->wlr_output->name);
}

void nauka_ipc_output_removed(struct nauka_output *output) {
  broadcast(output->server, "output destroy %s", output->wlr_output->name);
}

void nauka_ipc_layer_added(struct nauka_layer_surface *layer) {
  struct nauka_server *server = layer->server;
  layer->ipc_id = ++server->next_ipc_id;

  char namespace[256];
  sanitize(namespace, sizeof(namespace), layer->layer_surface->namespace);

  const char *output_name = "";
  if (layer->layer_surface->output != NULL)
    output_name = layer->layer_surface->output->name;

  broadcast(server, "layer new %u %s %s", layer->ipc_id, namespace,
            output_name);
}

void nauka_ipc_layer_removed(struct nauka_layer_surface *layer) {
  if (layer->ipc_id == 0)
    return;

  broadcast(layer->server, "layer destroy %u", layer->ipc_id);
  layer->ipc_id = 0;
}
