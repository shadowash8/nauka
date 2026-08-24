#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

enum nauka_keybind_action {
  NAUKA_ACTION_RUN,
  NAUKA_ACTION_EXIT,
  NAUKA_ACTION_RELOAD,
  NAUKA_ACTION_CLOSE_ACTIVE,
  NAUKA_ACTION_NEXT_TOPLEVEL,
  NAUKA_ACTION_VIEW_TAG,
  NAUKA_ACTION_MOVE_FOCUSED_TO_TAG,
  NAUKA_ACTION_TOGGLE_FLOATING,
};

struct nauka_keybind {
  uint32_t mods;
  xkb_keysym_t keysym;
  enum nauka_keybind_action action;
  char *command;
  struct nauka_keybind *next;
  int tag;
};

struct nauka_autostart {
  char *command;
  struct nauka_autostart *next;
};

struct nauka_config {
  uint32_t mod;
  uint32_t move_button;
  uint32_t resize_button;
  struct nauka_keybind *keybinds;

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

  struct nauka_autostart *autostart;
};

void parse_file(struct nauka_config *config, const char *path);
void config_run_autostart(struct nauka_config *config);
void config_load(struct nauka_config *config);
void config_reload(struct nauka_config *config);
void config_destroy(struct nauka_config *config);
