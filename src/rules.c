#include "rules.h"
#include "nauka.h"

#include <regex.h>
#include <string.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

static const char *predicate_name(enum nauka_rule_predicate p) {
  switch (p) {
  case NAUKA_RULE_FLOAT:
    return "float";
  case NAUKA_RULE_TILE:
    return "tile";
  case NAUKA_RULE_SIZE:
    return "size";
  case NAUKA_RULE_STICKY:
    return "sticky";
  case NAUKA_RULE_FULLSCREEN:
    return "fullscreen";
  case NAUKA_RULE_TAG:
    return "tag";
  case NAUKA_RULE_OPACITY:
    return "opacity";
  case NAUKA_RULE_RADIUS:
    return "radius";
  }
  return "?";
}

static bool rule_matches(const struct nauka_window_rule *rule,
                         const char *app_id, const char *title) {
  if (rule->match_app_id && regexec(&rule->app_id_re, app_id, 0, NULL, 0) != 0)
    return false;
  if (rule->match_title && regexec(&rule->title_re, title, 0, NULL, 0) != 0)
    return false;
  return true;
}

static void apply_size_rule(struct nauka_toplevel *toplevel,
                            const struct nauka_window_rule *rule,
                            const struct wlr_box *output_box) {
  int w = rule->size_w_pct ? (output_box->width * rule->size_w) / 100
                           : rule->size_w;
  int h = rule->size_h_pct ? (output_box->height * rule->size_h) / 100
                           : rule->size_h;
  if (w <= 0 || h <= 0)
    return; /* e.g. percentage size with no output yet */

  /* centered on the output, like the default floating geometry */
  int x = output_box->x;
  int y = output_box->y;
  if (output_box->width > 0)
    x = output_box->x + (output_box->width - w) / 2;
  if (output_box->height > 0)
    y = output_box->y + (output_box->height - h) / 2;

  toplevel->floating_geometry =
      (struct wlr_box){.x = x, .y = y, .width = w, .height = h};
  toplevel->geometry_from_rule = true;
}

void rules_apply(struct nauka_toplevel *toplevel,
                 const struct wlr_box *output_box) {
  const char *app_id = toplevel->xdg_toplevel->app_id;
  const char *title = toplevel->xdg_toplevel->title;
  if (app_id == NULL)
    app_id = "";
  if (title == NULL)
    title = "";

  if (toplevel->server->config.window_rules != NULL) {
    wlr_log(WLR_DEBUG, "window_rule: evaluating app_id='%s' title='%s'", app_id,
            title);
  }

  const struct nauka_window_rule *rule = toplevel->server->config.window_rules;
  for (; rule != NULL; rule = rule->next) {
    if (!rule_matches(rule, app_id, title))
      continue;

    wlr_log(WLR_DEBUG, "window_rule: %s matched app_id='%s' title='%s'",
            predicate_name(rule->predicate), app_id, title);

    switch (rule->predicate) {
    case NAUKA_RULE_FLOAT:
      toplevel->floating = true;
      break;
    case NAUKA_RULE_TILE:
      toplevel->floating = false;
      break;
    case NAUKA_RULE_SIZE:
      apply_size_rule(toplevel, rule, output_box);
      break;
    case NAUKA_RULE_STICKY:
      toplevel->sticky = true;
      toplevel->floating = true;
      break;
    case NAUKA_RULE_FULLSCREEN:
      toplevel->is_fullscreen = true;
      break;
    case NAUKA_RULE_TAG:
      toplevel->tag = rule->tag;
      break;
    case NAUKA_RULE_OPACITY:
      toplevel->opacity_active_override = rule->opacity_active;
      toplevel->opacity_inactive_override = rule->opacity_inactive;
      break;
    case NAUKA_RULE_RADIUS:
      toplevel->radius_override = rule->radius;
      toplevel->corner_radius = rule->radius;
      break;
    }
  }

  if (toplevel->server->config.window_rules != NULL) {
    wlr_log(WLR_DEBUG,
            "window_rule: result floating=%d sticky=%d fullscreen=%d tag=%d "
            "size=%dx%d opacity=%.2f/%.2f radius=%d",
            toplevel->floating, toplevel->sticky, toplevel->is_fullscreen,
            toplevel->tag, toplevel->floating_geometry.width,
            toplevel->floating_geometry.height,
            (double)toplevel->opacity_active_override,
            (double)toplevel->opacity_inactive_override,
            toplevel->radius_override);
  }
}
