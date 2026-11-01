#include "config.h"
#include "layout.h"
#include "tags.h"

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
#include <wlr/util/log.h>

static void config_set_defaults(struct nauka_config *config) {
  config->mod = WLR_MODIFIER_LOGO;
  config->move_button = BTN_LEFT;
  config->resize_button = BTN_RIGHT;
  config->keybinds = NULL;
  config->autostart = NULL;
  config->window_rules = NULL;
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

  config->shadow = false;
  config->shadow_floating_only = true;
  config->shadow_blur = 30;
  config->shadow_spread = 5;
  config->shadow_offset_x = 0;
  config->shadow_offset_y = 5;
  config->shadow_color[0] = 0;
  config->shadow_color[1] = 0;
  config->shadow_color[2] = 0;
  config->shadow_color[3] = 0.47f;

  config->xwayland = false;

  config->master_factor = 0.55;
  config->default_layout = NAUKA_LAYOUT_SCROLLER;
  config->new_window_at_bottom = false;

  strcpy(config->cursor_theme, "Adwaita");
  config->cursor_size = 24;
  strcpy(config->keyboard_layout, "us");
  config->keyboard_variant[0] = '\0';
  config->keyboard_options[0] = '\0';
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

/* Parses "1024" or "80%" into a value plus a percent flag. */
static bool parse_size_token(const char *token, int *value, bool *pct) {
  char buf[32];
  size_t len = strlen(token);
  bool is_pct = len > 0 && token[len - 1] == '%';

  if (is_pct) {
    if (len - 1 >= sizeof(buf))
      return false;
    memcpy(buf, token, len - 1);
    buf[len - 1] = '\0';
  } else {
    snprintf(buf, sizeof(buf), "%s", token);
  }

  char *end;
  int v = (int)strtol(buf, &end, 10);
  if (end == buf || v <= 0)
    return false;

  *value = v;
  *pct = is_pct;
  return true;
}

static bool compile_rule_pattern(const char *pattern, regex_t *re) {
  int err = regcomp(re, pattern, REG_EXTENDED | REG_NOSUB);
  if (err != 0) {
    char buf[256];
    regerror(err, re, buf, sizeof(buf));
    wlr_log(WLR_ERROR, "window_rule: invalid regex '%s': %s", pattern, buf);
    return false;
  }
  return true;
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

  if (strcmp(directive, "env") == 0) {
    char *key = next_token(&cursor);
    char *value = next_token(&cursor);

    if (key && value)
      setenv(key, value, 1);
    return;
  }

  if (strcmp(directive, "border_width") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL) {
      config->border_width = atoi(value);
    }
    return;
  }

  if (strcmp(directive, "animation_duration") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL) {
      char *end;
      config->animation_duration = (unsigned int)strtoul(value, &end, 10);
      if (end == value)
        config->animation_duration = 0;
    }
    return;
  }

  if (strcmp(directive, "animation_curve") == 0) {
    char *value = skip_ws(cursor);

    if (*value != '\0') {
      float a, b, c, d;

      if (sscanf(value, "%f , %f , %f , %f", &a, &b, &c, &d) == 4) {
        config->animation_curve[0] = a;
        config->animation_curve[1] = b;
        config->animation_curve[2] = c;
        config->animation_curve[3] = d;
      } else {
        config->animation_curve[0] = 0.42f;
        config->animation_curve[1] = 0.0f;
        config->animation_curve[2] = 0.58f;
        config->animation_curve[3] = 1.0f;
      }
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

  if (strcmp(directive, "shadow") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL) {
      config->shadow = strcasecmp(value, "true") == 0 ||
                       strcasecmp(value, "1") == 0 ||
                       strcasecmp(value, "yes") == 0;
    }
    return;
  }

  if (strcmp(directive, "shadow_blur") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL)
      config->shadow_blur = atoi(value);
    return;
  }

  if (strcmp(directive, "shadow_spread") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL)
      config->shadow_spread = atoi(value);
    return;
  }

  if (strcmp(directive, "shadow_floating_only") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL)
      config->shadow_floating_only = atoi(value);
    return;
  }

  if (strcmp(directive, "shadow_offset_x") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL)
      config->shadow_offset_x = atoi(value);
    return;
  }

  if (strcmp(directive, "shadow_offset_y") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL)
      config->shadow_offset_y = atoi(value);
    return;
  }

  if (strcmp(directive, "shadow_color") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL) {
      parse_hex_color(value, config->shadow_color);
    }
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

  if (strcmp(directive, "cursor_theme") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL)
      snprintf(config->cursor_theme, sizeof(config->cursor_theme), "%s", value);
    return;
  }

  if (strcmp(directive, "cursor_size") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL)
      config->cursor_size = atoi(value);
    return;
  }

  if (strcmp(directive, "keyboard_layout") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL)
      snprintf(config->keyboard_layout, sizeof(config->keyboard_layout), "%s",
               value);
    return;
  }

  if (strcmp(directive, "keyboard_variant") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL)
      snprintf(config->keyboard_variant, sizeof(config->keyboard_variant), "%s",
               value);
    return;
  }

  if (strcmp(directive, "keyboard_options") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL)
      snprintf(config->keyboard_options, sizeof(config->keyboard_options), "%s",
               value);
    return;
  }

  if (strcmp(directive, "default_layout") == 0) {
    char *value = next_token(&cursor);
    if (value == NULL)
      return;
    if (strcasecmp(value, "grid") == 0) {
      config->default_layout = NAUKA_LAYOUT_GRID;
    } else if (strcasecmp(value, "master") == 0) {
      config->default_layout = NAUKA_LAYOUT_MASTER;
    } else if (strcasecmp(value, "scroller") == 0) {
      config->default_layout = NAUKA_LAYOUT_SCROLLER;
    }
    return;
  }

  if (strcmp(directive, "master_factor") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL)
      config->master_factor = atof(value);
    return;
  }

  if (strcmp(directive, "new_window_position") == 0) {
    char *value = next_token(&cursor);
    if (value != NULL) {
      if (strcasecmp(value, "bottom") == 0)
        config->new_window_at_bottom = true;
      else if (strcasecmp(value, "top") == 0)
        config->new_window_at_bottom = false;
    }
    return;
  }

  if (strcmp(directive, "window_rule") == 0) {
    char *app_id_pat = next_token(&cursor);
    char *title_pat = next_token(&cursor);
    char *pred_str = next_token(&cursor);
    if (app_id_pat == NULL || title_pat == NULL || pred_str == NULL) {
      return;
    }

    struct nauka_window_rule *rule = calloc(1, sizeof(*rule));
    rule->match_app_id = strcmp(app_id_pat, "_") != 0;
    rule->match_title = strcmp(title_pat, "_") != 0;

    if (rule->match_app_id &&
        !compile_rule_pattern(app_id_pat, &rule->app_id_re)) {
      free(rule);
      return;
    }
    if (rule->match_title &&
        !compile_rule_pattern(title_pat, &rule->title_re)) {
      if (rule->match_app_id)
        regfree(&rule->app_id_re);
      free(rule);
      return;
    }

    bool ok = true;
    if (strcasecmp(pred_str, "float") == 0) {
      rule->predicate = NAUKA_RULE_FLOAT;
    } else if (strcasecmp(pred_str, "tile") == 0) {
      rule->predicate = NAUKA_RULE_TILE;
    } else if (strcasecmp(pred_str, "sticky") == 0) {
      rule->predicate = NAUKA_RULE_STICKY;
    } else if (strcasecmp(pred_str, "fullscreen") == 0) {
      rule->predicate = NAUKA_RULE_FULLSCREEN;
    } else if (strcasecmp(pred_str, "size") == 0) {
      char *w_str = next_token(&cursor);
      char *h_str = next_token(&cursor);
      if (w_str == NULL || h_str == NULL ||
          !parse_size_token(w_str, &rule->size_w, &rule->size_w_pct) ||
          !parse_size_token(h_str, &rule->size_h, &rule->size_h_pct)) {
        wlr_log(WLR_ERROR,
                "window_rule: size needs <width> <height> (px or %%): %s %s",
                w_str ? w_str : "(null)", h_str ? h_str : "(null)");
        ok = false;
      } else {
        rule->predicate = NAUKA_RULE_SIZE;
      }
    } else if (strcasecmp(pred_str, "tag") == 0) {
      char *value = next_token(&cursor);
      int tag = value != NULL ? atoi(value) : -1;
      if (tag < 1 || tag > NAUKA_TAG_COUNT) {
        wlr_log(WLR_ERROR, "window_rule: tag must be 1-%d", NAUKA_TAG_COUNT);
        ok = false;
      } else {
        rule->predicate = NAUKA_RULE_TAG;
        rule->tag = tag - 1; /* config uses 1-based labels, stores 0-based */
      }
    } else if (strcasecmp(pred_str, "opacity") == 0) {
      char *active_str = next_token(&cursor);
      char *inactive_str = next_token(&cursor);
      if (active_str == NULL) {
        wlr_log(WLR_ERROR, "window_rule: opacity needs <active> [<inactive>]");
        ok = false;
      } else {
        rule->predicate = NAUKA_RULE_OPACITY;
        rule->opacity_active = atof(active_str);
        rule->opacity_inactive =
            inactive_str != NULL ? atof(inactive_str) : rule->opacity_active;
      }
    } else if (strcasecmp(pred_str, "radius") == 0) {
      char *value = next_token(&cursor);
      if (value == NULL || atoi(value) < 0) {
        wlr_log(WLR_ERROR, "window_rule: radius needs a value >= 0");
        ok = false;
      } else {
        rule->predicate = NAUKA_RULE_RADIUS;
        rule->radius = atoi(value);
      }
    } else {
      wlr_log(WLR_ERROR, "window_rule: unknown predicate '%s'", pred_str);
      ok = false;
    }

    if (!ok) {
      if (rule->match_app_id)
        regfree(&rule->app_id_re);
      if (rule->match_title)
        regfree(&rule->title_re);
      free(rule);
      return;
    }

    /* append at the tail: rules must apply in file order so that later
     * matching rules override earlier ones */
    struct nauka_window_rule **tail = &config->window_rules;
    while (*tail != NULL)
      tail = &(*tail)->next;
    *tail = rule;
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
  } else if (strcasecmp(action_str, "swap_next_toplevel") == 0) {
    kb->action = NAUKA_ACTION_SWAP_NEXT_TOPLEVEL;
  } else if (strcasecmp(action_str, "swap_prev_toplevel") == 0) {
    kb->action = NAUKA_ACTION_SWAP_PREV_TOPLEVEL;
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
  } else if (strcasecmp(action_str, "layout") == 0) {
    char *layout_name = next_token(&cursor);
    if (layout_name == NULL) {
      free(kb);
      return;
    }
    if (strcasecmp(layout_name, "grid") == 0) {
      kb->layout = NAUKA_LAYOUT_GRID;
    } else if (strcasecmp(layout_name, "master") == 0) {
      kb->layout = NAUKA_LAYOUT_MASTER;
    } else if (strcasecmp(layout_name, "scroller") == 0) {
      kb->layout = NAUKA_LAYOUT_SCROLLER;
    } else {
      free(kb);
      return; /* unrecognized layout name */
    }
    kb->action = NAUKA_ACTION_SET_LAYOUT;
  } else if (strcasecmp(action_str, "grow_focused") == 0) {
    kb->action = NAUKA_ACTION_GROW_FOCUSED;
  } else if (strcasecmp(action_str, "shrink_focused") == 0) {
    kb->action = NAUKA_ACTION_SHRINK_FOCUSED;
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

  struct nauka_window_rule *rule = config->window_rules;
  while (rule) {
    struct nauka_window_rule *next = rule->next;
    if (rule->match_app_id)
      regfree(&rule->app_id_re);
    if (rule->match_title)
      regfree(&rule->title_re);
    free(rule);
    rule = next;
  }
  config->window_rules = NULL;
}
