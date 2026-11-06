#ifndef NAUKA_CONFIG_H
#define NAUKA_CONFIG_H

#include "layout.h"
#include <regex.h>
#include <stdint.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

enum nauka_keybind_action {
  NAUKA_ACTION_RUN,
  NAUKA_ACTION_EXIT,
  NAUKA_ACTION_RELOAD,
  NAUKA_ACTION_CLOSE_ACTIVE,
  NAUKA_ACTION_NEXT_TOPLEVEL,
  NAUKA_ACTION_PREV_TOPLEVEL,
  NAUKA_ACTION_SWAP_NEXT_TOPLEVEL,
  NAUKA_ACTION_SWAP_PREV_TOPLEVEL,
  NAUKA_ACTION_VIEW_TAG,
  NAUKA_ACTION_MOVE_FOCUSED_TO_TAG,
  NAUKA_ACTION_TOGGLE_FLOATING,
  NAUKA_ACTION_TOGGLE_FULLSCREEN,
  NAUKA_ACTION_TOGGLE_STICKY,
  NAUKA_ACTION_SET_LAYOUT,
  NAUKA_ACTION_GROW_FOCUSED,
  NAUKA_ACTION_SHRINK_FOCUSED,
};

struct nauka_keybind {
  uint32_t mods;
  xkb_keysym_t keysym;
  enum nauka_keybind_action action;
  char *command;
  struct nauka_keybind *next;
  int tag;
  int layout;
};

struct nauka_autostart {
  char *command;
  struct nauka_autostart *next;
};

enum nauka_rule_predicate {
  NAUKA_RULE_FLOAT,
  NAUKA_RULE_TILE,
  NAUKA_RULE_SIZE,
  NAUKA_RULE_STICKY,
  NAUKA_RULE_FULLSCREEN,
  NAUKA_RULE_TAG,
  NAUKA_RULE_OPACITY,
  NAUKA_RULE_RADIUS,
};

struct nauka_output_rule {
  char name[64];
  bool has_mode;
  int width, height;
  int refresh_mhz; /* 0 = highest available */
  bool has_scale;
  float scale;
  bool has_pos;
  int x, y;
  bool has_transform;
  enum wl_output_transform transform;
  bool has_adaptive_sync;
  bool adaptive_sync;
  struct nauka_output_rule *next;
};

struct nauka_window_rule {
  struct nauka_window_rule *next;
  regex_t app_id_re;
  regex_t title_re;
  bool match_app_id;
  bool match_title;
  enum nauka_rule_predicate predicate;
  int size_w, size_h;
  bool size_w_pct, size_h_pct;
  int tag;
  float opacity_active, opacity_inactive;
  int radius;
};

struct nauka_config {
  uint32_t mod;
  uint32_t move_button;
  uint32_t resize_button;
  struct nauka_keybind *keybinds;
  bool focus_follows_mouse;

  int border_width;
  float border_color_active[4];
  float border_color_inactive[4];
  int outer_gap;
  int inner_gap;

  int border_radius;
  float opacity_active;
  float opacity_inactive;
  bool blur;
  float blur_strength;
  float blur_alpha;

  bool shadow;
  bool shadow_floating_only;
  int shadow_blur;
  int shadow_spread;
  int shadow_offset_x;
  int shadow_offset_y;
  float shadow_color[4];

  unsigned int animation_duration;
  float animation_curve[4];

  struct nauka_autostart *autostart;
  struct nauka_window_rule *window_rules;
  struct nauka_output_rule *output_rules;
  bool xwayland;

  char cursor_theme[256];
  uint32_t cursor_size;
  char keyboard_layout[32];
  char keyboard_variant[32];
  char keyboard_options[256];

  enum nauka_layout_mode default_layout;
  double master_factor;
  bool new_window_at_bottom;
};

void parse_file(struct nauka_config *config, const char *path);
void config_run_autostart(struct nauka_config *config);
void config_set_path(const char *path);
void config_load(struct nauka_config *config);
void config_reload(struct nauka_config *config);
void config_destroy(struct nauka_config *config);

#endif
