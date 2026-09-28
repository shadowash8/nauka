#ifndef NAUKA_ANIMATION_H
#define NAUKA_ANIMATION_H

struct nauka_server;
struct nauka_toplevel;

void animation_init(struct nauka_server *server);
void animation_start(struct nauka_toplevel *t, int x, int y);

#endif
