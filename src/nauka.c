#include "nauka.h"
#include <assert.h>
#include <getopt.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/wlr_scene.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_ext_workspace_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

/* For brevity's sake, struct members are annotated where they are used. */
int main(int argc, char *argv[]) {
  wlr_log_init(WLR_DEBUG, NULL);
  char *startup_cmd = NULL;

  int c;
  while ((c = getopt(argc, argv, "s:h")) != -1) {
    switch (c) {
    case 's':
      startup_cmd = optarg;
      break;
    default:
      printf("Usage: %s [-s startup command]\n", argv[0]);
      return 0;
    }
  }
  if (optind < argc) {
    printf("Usage: %s [-s startup command]\n", argv[0]);
    return 0;
  }

  struct nauka_server server = {0};
  /* The Wayland display is managed by libwayland. It handles accepting
   * clients from the Unix socket, managing Wayland globals, and so on. */
  server.wl_display = wl_display_create();
  /* The backend is a wlroots feature which abstracts the underlying input and
   * output hardware. The autocreate option will choose the most suitable
   * backend based on the current environment, such as opening an X11 window
   * if an X11 server is running. */
  server.backend = wlr_backend_autocreate(
      wl_display_get_event_loop(server.wl_display), &server.session);
  if (server.backend == NULL) {
    wlr_log(WLR_ERROR, "failed to create wlr_backend");
    return 1;
  }

  /* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
   * can also specify a renderer using the WLR_RENDERER env var.
   * The renderer is responsible for defining the various pixel formats it
   * supports for shared memory, this configures that for clients. */
  server.renderer = fx_renderer_create(server.backend);
  if (server.renderer == NULL) {
    wlr_log(WLR_ERROR, "failed to create fx_renderer");
    return 1;
  }

  wlr_renderer_init_wl_display(server.renderer, server.wl_display);

  /* Autocreates an allocator for us.
   * The allocator is the bridge between the renderer and the backend. It
   * handles the buffer creation, allowing wlroots to render onto the
   * screen */
  server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
  if (server.allocator == NULL) {
    wlr_log(WLR_ERROR, "failed to create wlr_allocator");
    return 1;
  }

  /* This creates some hands-off wlroots interfaces. The compositor is
   * necessary for clients to allocate surfaces, the subcompositor allows to
   * assign the role of subsurfaces to surfaces and the data device manager
   * handles the clipboard. Each of these wlroots interfaces has room for you
   * to dig your fingers in and play with their behavior if you want. Note that
   * the clients cannot set the selection directly without compositor approval,
   * see the handling of the request_set_selection event below.*/
  wlr_compositor_create(server.wl_display, 5, server.renderer);
  wlr_subcompositor_create(server.wl_display);
  wlr_data_device_manager_create(server.wl_display);
  wlr_data_control_manager_v1_create(server.wl_display);
  wlr_screencopy_manager_v1_create(server.wl_display);
  wlr_viewporter_create(server.wl_display);

  /* Creates an output layout, which a wlroots utility for working with an
   * arrangement of screens in a physical layout. */
  server.output_layout = wlr_output_layout_create(server.wl_display);
  wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);
  server.output_manager = wlr_output_manager_v1_create(server.wl_display);
  server.output_manager_apply.notify = output_manager_apply;
  wl_signal_add(&server.output_manager->events.apply,
                &server.output_manager_apply);
  server.output_manager_test.notify = output_manager_test;
  wl_signal_add(&server.output_manager->events.test,
                &server.output_manager_test);

  /* Configure a listener to be notified when new outputs are available on the
   * backend. */
  wl_list_init(&server.outputs);
  server.new_output.notify = server_new_output;
  wl_signal_add(&server.backend->events.new_output, &server.new_output);

  /* Load config before anything that uses config */
  config_load(&server.config);

  /* Initialize fixed tags + ext-workspace-v1 */
  tags_init(&server);

  /* Create a scene graph. This is a wlroots abstraction that handles all
   * rendering and damage tracking. All the compositor author needs to do
   * is add things that should be rendered to the scene graph at the proper
   * positions and then call wlr_scene_output_commit() to render a frame if
   * necessary.
   */
  server.scene = wlr_scene_create();
  server.scene_layout =
      wlr_scene_attach_output_layout(server.scene, server.output_layout);

  server.background_tree = wlr_scene_tree_create(&server.scene->tree);
  server.bottom_tree = wlr_scene_tree_create(&server.scene->tree);
  server.toplevel_tree = wlr_scene_tree_create(&server.scene->tree);
  server.floating_tree = wlr_scene_tree_create(&server.scene->tree);
  server.blur_layer =
      wlr_scene_optimized_blur_create(&server.scene->tree, 0, 0);
  server.top_tree = wlr_scene_tree_create(&server.scene->tree);
  server.overlay_tree = wlr_scene_tree_create(&server.scene->tree);
  server.fullscreen_tree = wlr_scene_tree_create(&server.scene->tree);

  /* Set up xdg-shell version 3. The xdg-shell is a Wayland protocol which is
   * used for application windows. For more detail on shells, refer to
   * https://drewdevault.com/2018/07/29/Wayland-shells.html.
   */
  wl_list_init(&server.toplevels);
  server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
  server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
  wl_signal_add(&server.xdg_shell->events.new_toplevel,
                &server.new_xdg_toplevel);
  server.new_xdg_popup.notify = server_new_xdg_popup;
  wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

  /* Setup layer-shell */
  server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 4);

  server.new_layer_surface.notify = server_new_layer_surface;

  wl_signal_add(&server.layer_shell->events.new_surface,
                &server.new_layer_surface);

  /* Setup decoration manager */
  server.xdg_decoration_manager =
      wlr_xdg_decoration_manager_v1_create(server.wl_display);
  server.new_toplevel_decoration.notify = server_new_toplevel_decoration;
  wl_signal_add(&server.xdg_decoration_manager->events.new_toplevel_decoration,
                &server.new_toplevel_decoration);

  struct wlr_server_decoration_manager *server_decoration_manager =
      wlr_server_decoration_manager_create(server.wl_display);
  wlr_server_decoration_manager_set_default_mode(
      server_decoration_manager, WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

  /* Setup lock screen */
  server.lock_tree = wlr_scene_tree_create(&server.scene->tree);
  wlr_scene_node_set_enabled(&server.lock_tree->node, false);
  server.session_lock_manager =
      wlr_session_lock_manager_v1_create(server.wl_display);
  server.new_session_lock.notify = server_new_session_lock;
  wl_signal_add(&server.session_lock_manager->events.new_lock,
                &server.new_session_lock);

  /*
   * Creates a cursor, which is a wlroots utility for tracking the cursor
   * image shown on screen.
   */
  server.cursor = wlr_cursor_create();
  wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

  /* Creates an xcursor manager, another wlroots utility which loads up
   * Xcursor themes to source cursor images from and makes sure that cursor
   * images are available at all scale factors on the screen (necessary for
   * HiDPI support). */
  server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

  /*
   * wlr_cursor *only* displays an image on screen. It does not move around
   * when the pointer moves. However, we can attach input devices to it, and
   * it will generate aggregate events for all of them. In these events, we
   * can choose how we want to process them, forwarding them to clients and
   * moving the cursor around. More detail on this process is described in
   * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html.
   *
   * And more comments are sprinkled throughout the notify functions above.
   */
  server.cursor_mode = NAUKA_CURSOR_PASSTHROUGH;
  server.cursor_motion.notify = server_cursor_motion;
  wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
  server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
  wl_signal_add(&server.cursor->events.motion_absolute,
                &server.cursor_motion_absolute);
  server.cursor_button.notify = server_cursor_button;
  wl_signal_add(&server.cursor->events.button, &server.cursor_button);
  server.cursor_axis.notify = server_cursor_axis;
  wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
  server.cursor_frame.notify = server_cursor_frame;
  wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);
  server.relative_pointer_manager =
      wlr_relative_pointer_manager_v1_create(server.wl_display);
  server.pointer_constraints =
      wlr_pointer_constraints_v1_create(server.wl_display);
  server.new_pointer_constraint.notify = server_new_pointer_constraint;
  wl_signal_add(&server.pointer_constraints->events.new_constraint,
                &server.new_pointer_constraint);

  /*
   * Configures a seat, which is a single "seat" at which a user sits and
   * operates the computer. This conceptually includes up to one keyboard,
   * pointer, touch, and drawing tablet device. We also rig up a listener to
   * let us know when new input devices are available on the backend.
   */
  wl_list_init(&server.keyboards);
  server.new_input.notify = server_new_input;
  wl_signal_add(&server.backend->events.new_input, &server.new_input);
  server.seat = wlr_seat_create(server.wl_display, "seat0");
  server.request_cursor.notify = seat_request_cursor;
  wl_signal_add(&server.seat->events.request_set_cursor,
                &server.request_cursor);
  server.pointer_focus_change.notify = seat_pointer_focus_change;
  wl_signal_add(&server.seat->pointer_state.events.focus_change,
                &server.pointer_focus_change);
  server.request_set_selection.notify = seat_request_set_selection;
  wl_signal_add(&server.seat->events.request_set_selection,
                &server.request_set_selection);
  server.request_set_selection.notify = seat_request_set_selection;
  wl_signal_add(&server.seat->events.request_set_selection,
                &server.request_set_selection);
  server.request_start_drag.notify = seat_request_start_drag;
  wl_signal_add(&server.seat->events.request_start_drag,
                &server.request_start_drag);
  server.start_drag.notify = seat_start_drag;
  wl_signal_add(&server.seat->events.start_drag, &server.start_drag);

  server.kb_shortcuts_inhibit_manager =
      wlr_keyboard_shortcuts_inhibit_v1_create(server.wl_display);
  server.new_kb_shortcuts_inhibitor.notify = server_new_kb_shortcuts_inhibitor;
  wl_signal_add(&server.kb_shortcuts_inhibit_manager->events.new_inhibitor,
                &server.new_kb_shortcuts_inhibitor);

  /* Add a Unix socket to the Wayland display. */
  const char *socket = wl_display_add_socket_auto(server.wl_display);
  if (!socket) {
    wlr_backend_destroy(server.backend);
    return 1;
  }

  /* Start the backend. This will enumerate outputs and inputs, become the DRM
   * master, etc */
  if (!wlr_backend_start(server.backend)) {
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.wl_display);
    return 1;
  }

  /* Set the WAYLAND_DISPLAY environment variable to our socket and run the
   * startup command if requested. */
  setenv("WAYLAND_DISPLAY", socket, true);

  /* Launch xwayland-satellite for rootless X11 support, if enabled. */
  if (server.config.xwayland) {
    const char *x11_display = ":1";
    server.xwayland_satellite_pid = fork();
    if (server.xwayland_satellite_pid == 0) {
      execlp("xwayland-satellite", "xwayland-satellite", x11_display,
             (void *)NULL);
      _exit(1);
    } else if (server.xwayland_satellite_pid > 0) {
      setenv("DISPLAY", x11_display, true);
      wlr_log(WLR_INFO, "started xwayland-satellite on DISPLAY=%s (pid %d)",
              x11_display, server.xwayland_satellite_pid);
    } else {
      wlr_log(WLR_ERROR, "failed to fork for xwayland-satellite");
    }
  }

  if (startup_cmd) {
    if (fork() == 0) {
      execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
    }
  }

  config_run_autostart(&server.config);

  /* Run the Wayland event loop. This does not return until you exit the
   * compositor. Starting the backend rigged up all of the necessary event
   * loop configuration to listen to libinput events, DRM events, generate
   * frame events at the refresh rate, and so on. */
  wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
  wl_display_run(server.wl_display);

  /* Once wl_display_run returns, we destroy all clients then shut down the
   * server. */
  wl_display_destroy_clients(server.wl_display);

  if (server.config.xwayland && server.xwayland_satellite_pid > 0) {
    kill(server.xwayland_satellite_pid, SIGTERM);
    waitpid(server.xwayland_satellite_pid, NULL, 0);
  }

  wl_list_remove(&server.new_xdg_toplevel.link);
  wl_list_remove(&server.new_xdg_popup.link);

  wl_list_remove(&server.new_layer_surface.link);
  wl_list_remove(&server.new_toplevel_decoration.link);
  wl_list_remove(&server.new_session_lock.link);

  wl_list_remove(&server.cursor_motion.link);
  wl_list_remove(&server.cursor_motion_absolute.link);
  wl_list_remove(&server.cursor_button.link);
  wl_list_remove(&server.cursor_axis.link);
  wl_list_remove(&server.cursor_frame.link);
  wl_list_remove(&server.request_set_selection.link);
  wl_list_remove(&server.new_kb_shortcuts_inhibitor.link);

  wl_list_remove(&server.new_input.link);
  wl_list_remove(&server.request_cursor.link);
  wl_list_remove(&server.new_pointer_constraint.link);
  wl_list_remove(&server.pointer_focus_change.link);
  wl_list_remove(&server.request_set_selection.link);
  wl_list_remove(&server.request_start_drag.link);
  wl_list_remove(&server.start_drag.link);

  wl_list_remove(&server.new_output.link);
  wl_list_remove(&server.output_manager_apply.link);
  wl_list_remove(&server.output_manager_test.link);

  wlr_scene_node_destroy(&server.scene->tree.node);
  wlr_xcursor_manager_destroy(server.cursor_mgr);
  wlr_cursor_destroy(server.cursor);
  config_destroy(&server.config);
  wlr_allocator_destroy(server.allocator);
  wlr_renderer_destroy(server.renderer);
  wlr_backend_destroy(server.backend);
  wl_display_destroy(server.wl_display);
  return 0;
}
