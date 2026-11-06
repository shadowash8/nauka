#include "nauka.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <scenefx/types/wlr_scene.h>
#include <wayland-server-protocol.h>
#include <wlr/types/wlr_ext_workspace_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>

void output_frame(struct wl_listener *listener, void *data) {
  (void)data;

  /* This function is called every time an output is ready to display a frame,
   * generally at the output's refresh rate (e.g. 60Hz). */
  struct nauka_output *output = wl_container_of(listener, output, frame);
  struct wlr_scene *scene = output->server->scene;

  struct wlr_scene_output *scene_output =
      wlr_scene_get_scene_output(scene, output->wlr_output);

  /* Render the scene if needed and commit the output */
  if (!wlr_scene_output_commit(scene_output, NULL)) {
    return;
  }

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data) {
  /* This function is called when the backend requests a new state for
   * the output. For example, Wayland and X11 backends request a new mode
   * when the output window is resized. */
  struct nauka_output *output =
      wl_container_of(listener, output, request_state);
  const struct wlr_output_event_request_state *event = data;
  wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data) {
  (void)data;
  struct nauka_output *output = wl_container_of(listener, output, destroy);
  struct nauka_server *server = output->server;

  wlr_ext_workspace_group_handle_v1_output_leave(
      output->server->workspace_group, output->wlr_output);

  output->wlr_output->data = NULL;

  wl_list_remove(&output->frame.link);
  wl_list_remove(&output->request_state.link);
  wl_list_remove(&output->destroy.link);
  wl_list_remove(&output->link);

  session_lock_output_destroy(output);

  free(output);

  update_output_manager_config(server);
}

void update_output_manager_config(struct nauka_server *server) {
  struct wlr_output_configuration_v1 *config =
      wlr_output_configuration_v1_create();

  struct nauka_output *output;
  wl_list_for_each(output, &server->outputs, link) {
    struct wlr_output_configuration_head_v1 *head =
        wlr_output_configuration_head_v1_create(config, output->wlr_output);

    struct wlr_box output_box;
    wlr_output_layout_get_box(server->output_layout, output->wlr_output,
                              &output_box);

    head->state.enabled = output->wlr_output->enabled;
    head->state.x = output_box.x;
    head->state.y = output_box.y;
  }

  wlr_output_manager_v1_set_configuration(server->output_manager, config);
}

static bool apply_output_config(struct wlr_output_configuration_v1 *config,
                                struct nauka_server *server, bool test_only) {
  bool ok = true;

  struct wlr_output_configuration_head_v1 *head;
  wl_list_for_each(head, &config->heads, link) {
    struct wlr_output *wlr_output = head->state.output;
    struct nauka_output *output = wlr_output->data;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, head->state.enabled);

    if (head->state.enabled) {
      if (head->state.mode != NULL) {
        wlr_output_state_set_mode(&state, head->state.mode);
      } else {
        wlr_output_state_set_custom_mode(&state, head->state.custom_mode.width,
                                         head->state.custom_mode.height,
                                         head->state.custom_mode.refresh);
      }
      wlr_output_state_set_transform(&state, head->state.transform);
      wlr_output_state_set_scale(&state, head->state.scale);
      wlr_output_state_set_adaptive_sync_enabled(
          &state, head->state.adaptive_sync_enabled);
    }

    if (test_only) {
      ok &= wlr_output_test_state(wlr_output, &state);
      wlr_output_state_finish(&state);
      continue;
    }

    if (!wlr_output_commit_state(wlr_output, &state)) {
      /* config was rejected (bad scale/mode combo etc), retry with defaults
       * so the output isn't left stale, but still report the failure. */
      wlr_log(WLR_ERROR, "output %s: config rejected, using defaults",
              wlr_output->name);
      ok = false;
      wlr_output_state_finish(&state);
      wlr_output_state_init(&state);
      wlr_output_state_set_enabled(&state, true);
      struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
      if (mode)
        wlr_output_state_set_mode(&state, mode);
      wlr_output_commit_state(wlr_output, &state);
    }
    wlr_output_state_finish(&state);

    if (!ok) {
      continue;
    }

    if (head->state.enabled) {
      wlr_output_layout_add(server->output_layout, wlr_output, head->state.x,
                            head->state.y);
    } else {
      wlr_output_layout_remove(server->output_layout, wlr_output);
    }

    /* Mode/scale/transform/position all just changed. None of this
     * propagates on its own -- usable_area, layer-shell positions, the
     * blur backdrop size, and the tiling grid all need to be recomputed. */
    if (output != NULL && head->state.enabled) {
      arrange_layers(output);
      wlr_scene_optimized_blur_set_size(server->blur_layer, wlr_output->width,
                                        wlr_output->height);
      session_lock_update_output(output);
    }
  }

  if (!test_only) {
    /* Reload cursor theme at every scale currently in use so HiDPI
     * outputs get correctly rasterized cursors after a scale change. */
    struct nauka_output *o;
    wl_list_for_each(o, &server->outputs, link) {
      wlr_xcursor_manager_load(server->cursor_mgr, o->wlr_output->scale);
    }

    arrange_windows(server);
  }

  if (ok) {
    wlr_output_configuration_v1_send_succeeded(config);
  } else {
    wlr_output_configuration_v1_send_failed(config);
  }
  wlr_output_configuration_v1_destroy(config);

  return ok;
}

void output_manager_apply(struct wl_listener *listener, void *data) {
  struct nauka_server *server =
      wl_container_of(listener, server, output_manager_apply);
  struct wlr_output_configuration_v1 *config = data;

  apply_output_config(config, server, false);
  update_output_manager_config(server);
}

void output_manager_test(struct wl_listener *listener, void *data) {
  struct nauka_server *server =
      wl_container_of(listener, server, output_manager_test);
  struct wlr_output_configuration_v1 *config = data;

  apply_output_config(config, server, true);
}

static const struct nauka_output_rule *
find_output_rule(struct nauka_server *server, const char *name) {
  const struct nauka_output_rule *match = NULL;
  for (const struct nauka_output_rule *r = server->config.output_rules; r;
       r = r->next) {
    if (strcmp(r->name, "*") == 0 || strcmp(r->name, name) == 0)
      match = r; /* last match wins */
  }
  return match;
}

static struct wlr_output_mode *find_mode(struct wlr_output *wlr_output,
                                         const struct nauka_output_rule *rule) {
  struct wlr_output_mode *best = NULL;
  struct wlr_output_mode *mode;
  wl_list_for_each(mode, &wlr_output->modes, link) {
    if (mode->width != rule->width || mode->height != rule->height)
      continue;
    if (rule->refresh_mhz > 0) {
      /* allow 0.5 Hz slack so "60" matches 59.94 */
      if (abs(mode->refresh - rule->refresh_mhz) > 500)
        continue;
      if (best == NULL || abs(mode->refresh - rule->refresh_mhz) <
                              abs(best->refresh - rule->refresh_mhz))
        best = mode;
    } else if (best == NULL || mode->refresh > best->refresh) {
      best = mode;
    }
  }
  return best;
}

static void output_fill_state(const struct nauka_output_rule *rule,
                              struct wlr_output *wlr_output,
                              struct wlr_output_state *state) {
  struct wlr_output_mode *mode = NULL;
  if (rule && rule->has_mode) {
    mode = find_mode(wlr_output, rule);
    if (!mode) {
      wlr_log(WLR_ERROR, "output %s: mode %dx%d not supported, using preferred",
              wlr_output->name, rule->width, rule->height);
    }
  }
  if (!mode)
    mode = wlr_output_preferred_mode(wlr_output);
  if (mode)
    wlr_output_state_set_mode(state, mode);

  if (rule && rule->has_scale)
    wlr_output_state_set_scale(state, rule->scale);
  if (rule && rule->has_transform)
    wlr_output_state_set_transform(state, rule->transform);
  if (rule && rule->has_adaptive_sync)
    wlr_output_state_set_adaptive_sync_enabled(state, rule->adaptive_sync);
}

void server_new_output(struct wl_listener *listener, void *data) {
  (void)data;

  /* This event is raised by the backend when a new output (aka a display or
   * monitor) becomes available. */
  struct nauka_server *server = wl_container_of(listener, server, new_output);
  struct wlr_output *wlr_output = data;

  /* Configures the output created by the backend to use our allocator
   * and our renderer. Must be done once, before committing the output */
  wlr_output_init_render(wlr_output, server->allocator, server->renderer);

  /* Match the configured output rules before the first commit, so mode,
   * scale and transform take effect immediately. */
  const struct nauka_output_rule *rule =
      find_output_rule(server, wlr_output->name);

  /* The output may be disabled, switch it on. */
  struct wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, true);

  /* Some backends don't have modes. DRM+KMS does, and we need to set a mode
   * before we can use the output. The mode is a tuple of (width, height,
   * refresh rate), and each monitor supports only a specific set of modes.
   * output_fill_state() applies the rule's mode/scale/transform/adaptive
   * sync, or falls back to the monitor's preferred mode and defaults when no
   * rule matches. */
  output_fill_state(rule, wlr_output, &state);

  /* Atomically applies the new output state. */
  if (!wlr_output_commit_state(wlr_output, &state)) {
    wlr_log(WLR_ERROR, "output %s: failed to apply initial state",
            wlr_output->name);
  }
  wlr_output_state_finish(&state);

  /* The xcursor theme has to be loaded for this output's scale, otherwise
   * there is no cursor image at HiDPI scales. */
  if (server->cursor_mgr != NULL) {
    wlr_xcursor_manager_load(server->cursor_mgr, wlr_output->scale);
  }

  /* Allocates and configures our state for this output */
  struct nauka_output *output = calloc(1, sizeof(*output));
  output->wlr_output = wlr_output;
  output->server = server;
  wl_list_init(&output->layers);
  wlr_output->data = output;

  /* Sets up a listener for the frame event. */
  output->frame.notify = output_frame;
  wl_signal_add(&wlr_output->events.frame, &output->frame);

  /* Sets up a listener for the state request event. */
  output->request_state.notify = output_request_state;
  wl_signal_add(&wlr_output->events.request_state, &output->request_state);

  /* Sets up a listener for the destroy event. */
  output->destroy.notify = output_destroy;
  wl_signal_add(&wlr_output->events.destroy, &output->destroy);

  wl_list_insert(&server->outputs, &output->link);

  /* Adds this to the output layout. The add_auto function arranges outputs
   * from left-to-right in the order they appear. A more sophisticated
   * compositor would let the user configure the arrangement of outputs in the
   * layout.
   *
   * The output layout utility automatically adds a wl_output global to the
   * display, which Wayland clients can see to find out information about the
   * output (such as DPI, scale factor, manufacturer, etc).
   */
  struct wlr_output_layout_output *l_output =
      (rule && rule->has_pos)
          ? wlr_output_layout_add(server->output_layout, wlr_output, rule->x,
                                  rule->y)
          : wlr_output_layout_add_auto(server->output_layout, wlr_output);
  struct wlr_scene_output *scene_output =
      wlr_scene_output_create(server->scene, wlr_output);
  wlr_scene_output_layout_add_output(server->scene_layout, l_output,
                                     scene_output);
  wlr_scene_optimized_blur_set_size(server->blur_layer, wlr_output->width,
                                    wlr_output->height);
  wlr_ext_workspace_group_handle_v1_output_enter(server->workspace_group,
                                                 wlr_output);
  arrange_layers(output);
  session_lock_create_output_state(output);
  update_output_manager_config(server);
  arrange_windows(server);
}

void gamma_control_set_gamma(struct wl_listener *listener, void *data) {
  struct nauka_server *server = wl_container_of(listener, server, set_gamma);

  struct wlr_gamma_control_manager_v1_set_gamma_event *event = data;

  struct wlr_output_state state;
  wlr_output_state_init(&state);

  struct wlr_gamma_control_v1 *control =
      wlr_gamma_control_manager_v1_get_control(server->gamma_control_manager,
                                               event->output);

  if (!wlr_gamma_control_v1_apply(control, &state)) {
    wlr_output_state_finish(&state);
    return;
  }

  if (!wlr_output_commit_state(event->output, &state)) {
    wlr_gamma_control_v1_send_failed_and_destroy(control);
  }

  wlr_output_state_finish(&state);
}

void output_reload_config(struct nauka_server *server) {
  struct nauka_output *output;
  wl_list_for_each(output, &server->outputs, link) {
    const struct nauka_output_rule *rule =
        find_output_rule(server, output->wlr_output->name);
    if (!rule)
      continue;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    output_fill_state(rule, output->wlr_output, &state);
    wlr_output_commit_state(output->wlr_output, &state);
    wlr_output_state_finish(&state);

    if (rule->has_pos)
      wlr_output_layout_add(server->output_layout, output->wlr_output, rule->x,
                            rule->y);

    arrange_layers(output);
    wlr_scene_optimized_blur_set_size(server->blur_layer,
                                      output->wlr_output->width,
                                      output->wlr_output->height);
    session_lock_update_output(output);
  }
  wl_list_for_each(output, &server->outputs, link) {
    wlr_xcursor_manager_load(server->cursor_mgr, output->wlr_output->scale);
  }
  update_output_manager_config(server);
  arrange_windows(server);
}
