#include "config.h"

#include <libgen.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <wlr/types/wlr_keyboard.h>

static void config_set_defaults(struct nauka_config *config) {
  config->mod = WLR_MODIFIER_LOGO;
  config->move_button = BTN_LEFT;
  config->resize_button = BTN_RIGHT;
  config->keybinds = NULL;
  config->focus_follows_mouse = false;

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

  config->border_radius = 12;
  config->opacity_active = 1.0f;
  config->opacity_inactive = 1.0f;

  config->blur = false;
  config->blur_strength = 3.0f;
  config->blur_alpha = 1.0f;

  config->xwayland = false;
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

struct imported_file {
  char path[PATH_MAX];
  struct imported_file *next;
};

static struct imported_file *imports = NULL;

static bool already_imported(const char *path) {
  for (struct imported_file *it = imports; it; it = it->next) {
    if (strcmp(it->path, path) == 0)
      return true;
  }
  return false;
}

static void mark_imported(const char *path) {
  struct imported_file *f = calloc(1, sizeof(*f));
  strcpy(f->path, path);
  f->next = imports;
  imports = f;
}

static bool expand_path(const char *input, const char *current_dir, char *out) {
  if (input[0] == '/') {
    snprintf(out, PATH_MAX, "%s", input);
  } else if (input[0] == '~' && input[1] == '/') {
    const char *home = getenv("HOME");
    if (!home)
      return false;
    snprintf(out, PATH_MAX, "%s/%s", home, input + 2);
  } else {
    snprintf(out, PATH_MAX, "%s/%s", current_dir, input);
  }

  char resolved[PATH_MAX];
  if (realpath(out, resolved))
    strcpy(out, resolved);

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

static void config_parse_line(struct nauka_config *config, char *line,
                              const char *current_dir) {
  line[strcspn(line, "\r\n")] = '\0';

  char *trimmed = skip_ws(line);
  if (trimmed[0] == '#' || trimmed[0] == '\0') {
    return; /* whole-line comment or blank */
  }

  if (strncmp(trimmed, "import", 6) == 0) {
    char *path = skip_ws(trimmed + 6);

    size_t len = strlen(path);
    if (len >= 2 && path[0] == '"' && path[len - 1] == '"') {
      path[len - 1] = '\0';
      path++;
    }

    if (*path) {
      char full[PATH_MAX];
      if (expand_path(path, current_dir, full))
        parse_file(config, full);
    }
    return;
  }

  char *cursor = trimmed;
  char *directive = next_token(&cursor);
  if (directive == NULL) {
    return;
  }

  if (strcmp(directive, "run") == 0) {
    char *rest = skip_ws(cursor);

    size_t len = strlen(rest);
    while (len > 0 && (rest[len - 1] == ' ' || rest[len - 1] == '\t'))
      rest[--len] = '\0';

    if (len >= 2 && rest[0] == '"' && rest[len - 1] == '"') {
      rest[len - 1] = '\0';
      rest++;
    }

    if (*rest) {
      struct nauka_autostart *a = calloc(1, sizeof(*a));
      a->command = strdup(rest);
      a->next = config->autostart;
      config->autostart = a;
    }

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

  if (strcmp(directive, "border_radius") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL) {
      config->border_radius = atoi(value);
    }
    return;
  }

  if (strcmp(directive, "opacity_active") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL)
      config->opacity_active = atof(value);
    return;
  }

  if (strcmp(directive, "opacity_inactive") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL)
      config->opacity_inactive = atof(value);
    return;
  }

  if (strcmp(directive, "blur") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL) {
      config->blur = strcasecmp(value, "true") == 0 ||
                     strcasecmp(value, "1") == 0 ||
                     strcasecmp(value, "yes") == 0;
    }
    return;
  }

  if (strcmp(directive, "blur_strength") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL)
      config->blur_strength = atof(value);
    return;
  }

  if (strcmp(directive, "blur_alpha") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL)
      config->blur_alpha = atof(value);
    return;
  }

  if (strcmp(directive, "xwayland") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL) {
      config->xwayland = strcasecmp(value, "true") == 0 ||
                         strcasecmp(value, "1") == 0 ||
                         strcasecmp(value, "yes") == 0;
    }
    return;
  }

  if (strcmp(directive, "focus_follows_mouse") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL) {
      config->focus_follows_mouse = strcasecmp(value, "true") == 0 ||
                                    strcasecmp(value, "1") == 0 ||
                                    strcasecmp(value, "yes") == 0;
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
  } else if (strcasecmp(action_str, "prev_toplevel") == 0) {
    kb->action = NAUKA_ACTION_PREV_TOPLEVEL;
  } else if (strcasecmp(action_str, "view_tag") == 0) {
    kb->action = NAUKA_ACTION_VIEW_TAG;
    kb->tag = keysym - XKB_KEY_1;
  } else if (strcasecmp(action_str, "move_focused_to_tag") == 0) {
    kb->action = NAUKA_ACTION_MOVE_FOCUSED_TO_TAG;
    kb->tag = keysym - XKB_KEY_1;
  } else if (strcasecmp(action_str, "toggle_floating") == 0) {
    kb->action = NAUKA_ACTION_TOGGLE_FLOATING;
  } else if (strcasecmp(action_str, "toggle_fullscreen") == 0) {
    kb->action = NAUKA_ACTION_TOGGLE_FULLSCREEN;
  } else if (strcasecmp(action_str, "toggle_sticky") == 0) {
    kb->action = NAUKA_ACTION_TOGGLE_STICKY;
  } else {
    free(kb);
    return;
  }

  kb->next = config->keybinds;
  config->keybinds = kb;
}

void config_run_autostart(struct nauka_config *config) {
  for (struct nauka_autostart *a = config->autostart; a; a = a->next) {
    if (fork() == 0) {
      execl("/bin/sh", "sh", "-c", a->command, (char *)NULL);
      _exit(1);
    }
  }
}

void config_load(struct nauka_config *config) {
  config_set_defaults(config);

  while (imports) {
    struct imported_file *next = imports->next;
    free(imports);
    imports = next;
  }

  char path[PATH_MAX];
  config_get_path(path, sizeof(path));

  if (path[0] == '\0')
    return;

  parse_file(config, path);
}

void parse_file(struct nauka_config *config, const char *path) {
  char resolved[PATH_MAX];

  if (!realpath(path, resolved))
    return;

  if (already_imported(resolved))
    return;

  mark_imported(resolved);

  FILE *fp = fopen(resolved, "r");
  if (!fp)
    return;

  char dir[PATH_MAX];
  strcpy(dir, resolved);
  dirname(dir);

  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    config_parse_line(config, line, dir);
  }

  fclose(fp);
}

void config_reload(struct nauka_config *config) {
  /* clear import cache */
  while (imports) {
    struct imported_file *next = imports->next;
    free(imports);
    imports = next;
  }

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

  struct nauka_autostart *a = config->autostart;
  while (a) {
    struct nauka_autostart *next = a->next;
    free(a->command);
    free(a);
    a = next;
  }
  config->autostart = NULL;
}
