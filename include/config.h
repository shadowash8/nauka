#ifndef NAUKA_CONFIG_H
#define NAUKA_CONFIG_H

#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

enum nauka_keybind_action {
  NAUKA_ACTION_RUN,
  NAUKA_ACTION_EXIT,
  NAUKA_ACTION_CLOSE_ACTIVE,
  NAUKA_ACTION_NEXT_TOPLEVEL,
};

struct nauka_keybind {
  uint32_t mods;
  xkb_keysym_t keysym;
  enum nauka_keybind_action action;
  char *command;
  struct nauka_keybind *next;
};

struct nauka_config {
  uint32_t mod;
  uint32_t move_button;
  uint32_t resize_button;
  struct nauka_keybind *keybinds;

  int border_width;
  float border_color_active[4];
  float border_color_inactive[4];
};

void config_load(struct nauka_config *config);
void config_destroy(struct nauka_config *config);

#endif
