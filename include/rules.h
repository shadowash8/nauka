#ifndef NAUKA_RULES_H
#define NAUKA_RULES_H

struct nauka_toplevel;
struct wlr_box;

void rules_apply(struct nauka_toplevel *toplevel,
                 const struct wlr_box *output_box);

#endif
