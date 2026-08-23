#include "config.h"

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_keyboard.h>

static void config_set_defaults(struct nauka_config *config) {
  config->mod = WLR_MODIFIER_LOGO;
  config->move_button = BTN_LEFT;
  config->resize_button = BTN_RIGHT;
  config->keybinds = NULL;

  config->border_width = 2;
  config->border_color_active[0] = 0.9f;
  config->border_color_active[1] = 0.6f;
  config->border_color_active[2] = 0.2f;
  config->border_color_active[3] = 1.0f;

  config->border_color_inactive[0] = 0.3f;
  config->border_color_inactive[1] = 0.3f;
  config->border_color_inactive[2] = 0.3f;
  config->border_color_inactive[3] = 1.0f;
  config->outer_gap = 10;
  config->inner_gap = 10;
}

/* Parses "#rrggbb" or "#rrggbbaa" into a float[4] rgba. Returns false on
 * malformed input, leaving out unchanged. */
static bool parse_hex_color(const char *value, float out[4]) {
  if (value[0] != '#') {
    return false;
  }
  size_t len = strlen(value + 1);
  if (len != 6 && len != 8) {
    return false;
  }

  unsigned int r, g, b, a = 255;
  int n;
  if (len == 6) {
    n = sscanf(value + 1, "%02x%02x%02x", &r, &g, &b);
    if (n != 3)
      return false;
  } else {
    n = sscanf(value + 1, "%02x%02x%02x%02x", &r, &g, &b, &a);
    if (n != 4)
      return false;
  }

  out[0] = r / 255.0f;
  out[1] = g / 255.0f;
  out[2] = b / 255.0f;
  out[3] = a / 255.0f;
  return true;
}

static uint32_t parse_modifier(const char *value) {
  if (strcasecmp(value, "super") == 0 || strcasecmp(value, "logo") == 0) {
    return WLR_MODIFIER_LOGO;
  }
  if (strcasecmp(value, "alt") == 0) {
    return WLR_MODIFIER_ALT;
  }
  if (strcasecmp(value, "ctrl") == 0 || strcasecmp(value, "control") == 0) {
    return WLR_MODIFIER_CTRL;
  }
  if (strcasecmp(value, "shift") == 0) {
    return WLR_MODIFIER_SHIFT;
  }
  return 0; /* unknown token contributes nothing */
}

/* Handles combos "_" meaning no modifier. */
static uint32_t parse_modifiers(const char *value) {
  if (strcmp(value, "_") == 0) {
    return 0;
  }

  char buf[128];
  snprintf(buf, sizeof(buf), "%s", value);

  uint32_t mods = 0;
  char *saveptr = NULL;
  char *tok = strtok_r(buf, "+", &saveptr);
  while (tok != NULL) {
    mods |= parse_modifier(tok);
    tok = strtok_r(NULL, "+", &saveptr);
  }
  return mods;
}

static uint32_t parse_pointer_button(const char *value) {
  if (strcasecmp(value, "pointer_left_click") == 0)
    return BTN_LEFT;
  if (strcasecmp(value, "pointer_right_click") == 0)
    return BTN_RIGHT;
  if (strcasecmp(value, "pointer_middle_click") == 0)
    return BTN_MIDDLE;
  return 0;
}

static void config_get_path(char *buf, size_t len) {
  const char *xdg_config = getenv("XDG_CONFIG_HOME");
  const char *home = getenv("HOME");
  if (xdg_config != NULL) {
    snprintf(buf, len, "%s/nauka/nauka.conf", xdg_config);
  } else if (home != NULL) {
    snprintf(buf, len, "%s/.config/nauka/nauka.conf", home);
  } else {
    buf[0] = '\0';
  }
}

static char *skip_ws(char *s) {
  while (*s == ' ' || *s == '\t')
    s++;
  return s;
}

static char *next_token(char **cursor) {
  char *s = skip_ws(*cursor);
  if (*s == '\0') {
    *cursor = s;
    return NULL;
  }
  char *start = s;
  while (*s != '\0' && *s != ' ' && *s != '\t')
    s++;
  if (*s != '\0') {
    *s = '\0';
    s++;
  }
  *cursor = s;
  return start;
}

static void config_parse_line(struct nauka_config *config, char *line) {
  line[strcspn(line, "\r\n")] = '\0';

  char *trimmed = skip_ws(line);
  if (trimmed[0] == '#' || trimmed[0] == '\0') {
    return; /* whole-line comment or blank */
  }

  char *cursor = trimmed;
  char *directive = next_token(&cursor);
  if (directive == NULL) {
    return;
  }

  if (strcmp(directive, "border_width") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL) {
      config->border_width = atoi(value);
    }
    return;
  }

  if (strcmp(directive, "border_color_active") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL) {
      parse_hex_color(value, config->border_color_active);
    }
    return;
  }

  if (strcmp(directive, "border_color_inactive") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL) {
      parse_hex_color(value, config->border_color_inactive);
    }
    return;
  }

  if (strcmp(directive, "inner_gap") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL) {
      config->inner_gap = atoi(value);
    }
    return;
  }

  if (strcmp(directive, "outer_gap") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL) {
      config->outer_gap = atoi(value);
    }
    return;
  }

  if (strcmp(directive, "keybind") != 0) {
    return;
  }

  char *mods_str = next_token(&cursor);
  char *trigger_str = next_token(&cursor);
  char *action_str = next_token(&cursor);
  if (mods_str == NULL || trigger_str == NULL || action_str == NULL) {
    return;
  }

  uint32_t mods = parse_modifiers(mods_str);

  /* Pointer-click keybind (move/resize), same as before. */
  uint32_t button = parse_pointer_button(trigger_str);
  if (button != 0) {
    if (strcasecmp(action_str, "move") == 0) {
      config->mod = mods;
      config->move_button = button;
    } else if (strcasecmp(action_str, "resize") == 0) {
      config->mod = mods;
      config->resize_button = button;
    }
    return;
  }

  /* Otherwise, treat trigger_str as a real keyboard key name. */
  xkb_keysym_t keysym =
      xkb_keysym_from_name(trigger_str, XKB_KEYSYM_CASE_INSENSITIVE);
  if (keysym == XKB_KEY_NoSymbol) {
    return; /* unrecognized key name */
  }

  struct nauka_keybind *kb = calloc(1, sizeof(*kb));
  kb->mods = mods;
  kb->keysym = keysym;

  if (strcasecmp(action_str, "run") == 0) {
    char *rest = skip_ws(cursor);
    size_t len = strlen(rest);
    while (len > 0 && (rest[len - 1] == ' ' || rest[len - 1] == '\t')) {
      rest[--len] = '\0';
    }
    if (len >= 2 && rest[0] == '"' && rest[len - 1] == '"') {
      rest[len - 1] = '\0';
      rest++;
    }
    if (rest[0] == '\0') {
      free(kb);
      return;
    }
    kb->action = NAUKA_ACTION_RUN;
    kb->command = strdup(rest);
  } else if (strcasecmp(action_str, "exit") == 0) {
    kb->action = NAUKA_ACTION_EXIT;
  } else if (strcasecmp(action_str, "reload") == 0) {
    kb->action = NAUKA_ACTION_RELOAD;
  } else if (strcasecmp(action_str, "kill_active") == 0) {
    kb->action = NAUKA_ACTION_CLOSE_ACTIVE;
  } else if (strcasecmp(action_str, "next_toplevel") == 0) {
    kb->action = NAUKA_ACTION_NEXT_TOPLEVEL;
  } else if (strcasecmp(action_str, "view_tag") == 0) {
    kb->action = NAUKA_ACTION_VIEW_TAG;
    kb->tag = keysym - XKB_KEY_1;
  } else if (strcasecmp(action_str, "move_focused_to_tag") == 0) {
    kb->action = NAUKA_ACTION_MOVE_FOCUSED_TO_TAG;
    kb->tag = keysym - XKB_KEY_1;
  } else {
    free(kb);
    return;
  }

  kb->next = config->keybinds;
  config->keybinds = kb;
}

void config_load(struct nauka_config *config) {
  config_set_defaults(config);

  char path[512];
  config_get_path(path, sizeof(path));
  if (path[0] == '\0') {
    return;
  }

  FILE *f = fopen(path, "r");
  if (f == NULL) {
    return;
  }

  char line[256];
  while (fgets(line, sizeof(line), f) != NULL) {
    config_parse_line(config, line);
  }

  fclose(f);
}

void config_reload(struct nauka_config *config) {
  struct nauka_config new_config = {0};

  config_load(&new_config);

  config_destroy(config);
  *config = new_config;
}

void config_destroy(struct nauka_config *config) {
  struct nauka_keybind *kb = config->keybinds;
  while (kb != NULL) {
    struct nauka_keybind *next = kb->next;
    free(kb->command);
    free(kb);
    kb = next;
  }
  config->keybinds = NULL;
}
