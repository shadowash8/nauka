#define NAUKA_TAG_COUNT 9

struct nauka_server;

void view_tag(struct nauka_server *server, int tag);
void move_focused_to_tag(struct nauka_server *server, int tag);
