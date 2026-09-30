#include "animation.h"
#include "nauka.h"
#include <time.h>

// static const float anim_curve[4] = {0.0f, 0.85f, 0.2f, 1.0f};

static uint32_t now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static float bezier(float t, float anim_curve[4]) {
  float x1 = anim_curve[0], y1 = anim_curve[1];
  float x2 = anim_curve[2], y2 = anim_curve[3];
  float lo = 0, hi = 1, s = t, x;
  for (int i = 0; i < 20; i++) {
    x = 3*(1-s)*(1-s)*s*x1 + 3*(1-s)*s*s*x2 + s*s*s;
    if (x < t) lo = s; else hi = s;
    s = (lo + hi) / 2;
  }
  return 3*(1-s)*(1-s)*s*y1 + 3*(1-s)*s*s*y2 + s*s*s;
}

static int tick(void *data) {
  struct nauka_server *s = data;
  struct nauka_toplevel *t;
  bool more = false;

  wl_list_for_each(t, &s->toplevels, link) {
    if (!t->animating)
      continue;
    float p = (now_ms() - t->anim_start) / (float)s->config.animation_duration;
    if (p >= 1) {
      p = 1;
      t->animating = false;
    }
    p = bezier(p, s->config.animation_curve);
    wlr_scene_node_set_position(&t->scene_tree->node,
        t->anim_from.x + (t->anim_to.x - t->anim_from.x) * p,
        t->anim_from.y + (t->anim_to.y - t->anim_from.y) * p);
    more |= t->animating;
  }
  if (more)
    wl_event_source_timer_update(s->anim_timer, 16);
  return 0;
}

void animation_start(struct nauka_toplevel *t, int x, int y) {
  if (t->animating && t->anim_to.x == x && t->anim_to.y == y)
    return;
  if (!t->animating && t->scene_tree->node.x == x && t->scene_tree->node.y == y)
    return;

  t->anim_from = (struct wlr_box){t->scene_tree->node.x, t->scene_tree->node.y, 0, 0};
  t->anim_to = (struct wlr_box){x, y, 0, 0};
  t->anim_start = now_ms();
  t->animating = true;
  wl_event_source_timer_update(t->server->anim_timer, 1);
}

void animation_init(struct nauka_server *s) {
  s->anim_timer = wl_event_loop_add_timer(
      wl_display_get_event_loop(s->wl_display), tick, s);
}
