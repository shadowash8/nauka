struct nauka_server;

enum nauka_layout_mode {
  NAUKA_LAYOUT_GRID = 0,
  NAUKA_LAYOUT_MASTER,
  NAUKA_LAYOUT_COUNT,
};

void arrange_windows(struct nauka_server *server);
void layout_set(struct nauka_server *server, enum nauka_layout_mode mode);
void layout_cycle(struct nauka_server *server);
