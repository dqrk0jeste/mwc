#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/wlr_scene.h>

#include "mwc.h"

#include "helpers.h"
#include "ipc.h"
#include "keyboard.h"
#include "config.h"
#include "output.h"
#include "rendering.h"
#include "toplevel.h"
#include "popup.h"
#include "layer_surface.h"
#include "decoration.h"
#include "dnd.h"
#include "gamma_control.h"
#include "session_lock.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include "wlr/util/log.h"
#include "wlr/types/wlr_seat.h"
#include <wlr/backend/session.h>
#include "wlr/types/wlr_cursor.h"
#include "wlr/types/wlr_data_device.h"
#include "wlr/backend.h"
#include "wlr/render/allocator.h"
#include "wlr/types/wlr_compositor.h"
#include "wlr/types/wlr_subcompositor.h"
#include "wlr/types/wlr_subcompositor.h"
#include "wlr/types/wlr_xdg_output_v1.h"
#include "wlr/types/wlr_xcursor_manager.h"
#include "wlr/types/wlr_xdg_decoration_v1.h"
#include "wlr/types/wlr_data_control_v1.h"
#include "wlr/types/wlr_screencopy_v1.h"
#include "wlr/types/wlr_viewporter.h"
#include "wlr/types/wlr_foreign_toplevel_management_v1.h"
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_session_lock_v1.h>

/* we initialize an instance of our global state */
struct mwc_server server;

/* handles child processes */
void
sigchld_handler(int signo) {
  while(waitpid(-1, NULL, WNOHANG) > 0);
}

void
server_handle_new_input(struct wl_listener *listener, void *data) {
  struct wlr_input_device *input = data;

  switch(input->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
      server_handle_new_keyboard(input);
      break;
    case WLR_INPUT_DEVICE_POINTER:
      server_handle_new_pointer(input);
      break;
    default:
      /* mwc doesnt support touch devices, drawing tablets etc */
      break;
  }

  /* we need to let the wlr_seat know what our capabilities are, which is
   * communiciated to the client. we always have a cursor, even if
   * there are no pointer devices, so we always include that capability. */
  uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
  if(!wl_list_empty(&server.keyboards)) {
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  }

  wlr_seat_set_capabilities(server.seat, caps);
}

void
server_handle_cursor_shape_destroy(struct wl_listener *listener, void *data) {
  wl_list_remove(&server.request_cursor_shape.link);
  wl_list_remove(&server.cursor_shape_manager_destroy.link);
}

void
server_handle_request_cursor_shape(struct wl_listener *listener, void *data) {
  struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
  struct wlr_seat_client *focused_client = server.seat->pointer_state.focused_client;
  if(focused_client == event->seat_client) {
    const char *name = wlr_cursor_shape_v1_name(event->shape);
    wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, name);
    server.client_cursor.surface = NULL;
  }
}

void 
server_handle_request_cursor(struct wl_listener *listener, void *data) {
  struct wlr_seat_pointer_request_set_cursor_event *event = data;
  struct wlr_seat_client *focused_client = server.seat->pointer_state.focused_client;
  if(focused_client == event->seat_client) {
    /* once we've vetted the client, we can tell the cursor to use the
     * provided surface as the cursor image. it will set the hardware cursor
     * on the output that it's currently on and continue to do so as the
     * cursor moves between outputs */
    wlr_cursor_set_surface(server.cursor, event->surface,
                           event->hotspot_x, event->hotspot_y);
    /* TODO: maybe this should be placed elsewhere */
    server.client_cursor.surface = event->surface;
    server.client_cursor.hotspot_x = event->hotspot_x;
    server.client_cursor.hotspot_y = event->hotspot_y;
  }
}

void
server_handle_request_set_selection(struct wl_listener *listener, void *data) {
  /* this event is raised by the seat when a client wants to set the selection,
   * usually when the user copies something. wlroots allows compositors to
   * ignore such requests if they so choose, but in mwc we always honor
   */
  struct wlr_seat_request_set_selection_event *event = data;
  wlr_seat_set_selection(server.seat, event->source, event->serial);
}

int
main(int argc, char *argv[]) {
  /* this is ripped straight from chatgpt, it prevents the creation of zombie processes. */
  struct sigaction sa;
  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGCHLD, &sa, NULL);

  bool debug = false;
  for(int i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--debug") == 0) {
      debug = true;
    }
  }

  mkdir("/tmp/mwc", 0777);
  if(debug) {
    /* make it so all the logs do to the log file */
    FILE *logs = fopen("/tmp/mwc/logs", "w");
    if(logs != NULL) {
      int fd = fileno(logs);
      close(1);
      close(2);
      dup2(fd, 1);
      dup2(fd, 2);
      fclose(logs);
    }

    wlr_log_init(WLR_DEBUG, NULL);
  } else {
    wlr_log_init(WLR_INFO, NULL);
  }

  server.config = config_load();
  if(server.config == NULL) {
    wlr_log(WLR_ERROR, "there was a problem loading the config, quiting");
    return 1;
  }

  /* The Wayland display is managed by libwayland. It handles accepting
   * clients from the Unix socket, manging Wayland globals, and so on. */
  server.wl_display = wl_display_create();
  server.wl_event_loop = wl_display_get_event_loop(server.wl_display);

  /* The backend is a wlroots feature which abstracts the underlying input and
   * output hardware. The autocreate option will choose the most suitable
   * backend based on the current environment, such as opening an X11 window
   * if an X11 server is running. */
  server.backend = wlr_backend_autocreate(server.wl_event_loop, &server.session);
  if(server.backend == NULL) {
    wlr_log(WLR_ERROR, "failed to create wlr_backend");
    return 1;
  }

  /* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
   * can also specify a renderer using the WLR_RENDERER env var.
   * The renderer is responsible for defining the various pixel formats it
   * supports for shared memory, this configures that for clients. */
  server.renderer = fx_renderer_create(server.backend);
  if(server.renderer == NULL) {
    wlr_log(WLR_ERROR, "failed to create wlr_renderer");
    return 1;
  }

  wlr_renderer_init_wl_display(server.renderer, server.wl_display);

  /* Autocreates an allocator for us.
   * The allocator is the bridge between the renderer and the backend. It
   * handles the buffer creation, allowing wlroots to render onto the
   * screen */
  server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
  if(server.allocator == NULL) {
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

  /* Creates an output layout, which a wlroots utility for working with an
   * arrangement of screens in a physical layout. */
  server.output_layout = wlr_output_layout_create(server.wl_display);

  /* Configure a listener to be notified when new outputs are available on the
   * backend. */
  wl_list_init(&server.outputs);
  server.new_output.notify = server_handle_new_output;
  wl_signal_add(&server.backend->events.new_output, &server.new_output);

  /* Create a scene graph. This is a wlroots abstraction that handles all
   * rendering and damage tracking. All the compositor author needs to do
   * is add things that should be rendered to the scene graph at the proper
   * positions and then call wlr_scene_output_commit() to render a frame if
   * necessary.
   */
  server.scene = wlr_scene_create();
  server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

  /* create all the scenes in the correct order */
  server.background_tree = wlr_scene_tree_create(&server.scene->tree);
  server.bottom_tree = wlr_scene_tree_create(&server.scene->tree);
  server.tiled_tree = wlr_scene_tree_create(&server.scene->tree);
  server.floating_tree = wlr_scene_tree_create(&server.scene->tree);
  server.top_tree = wlr_scene_tree_create(&server.scene->tree);
  server.fullscreen_tree = wlr_scene_tree_create(&server.scene->tree);
  server.overlay_tree = wlr_scene_tree_create(&server.scene->tree);
  server.session_lock_tree = wlr_scene_tree_create(&server.scene->tree);

  /* set up xdg-shell version 6 */
  server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 6);
  server.new_xdg_toplevel.notify = server_handle_new_toplevel;
  wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
  server.new_xdg_popup.notify = server_handle_new_popup;
  wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

  server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 4);
  server.new_layer_surface.notify = server_handle_new_layer_surface;
  server.layer_shell->data = &server;
  wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

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
  server.cursor_mgr = wlr_xcursor_manager_create(server.config->cursor_theme,
                                                 server.config->cursor_size);
  /* we also add xcursor theme env variables */
  char cursor_size[8];
  snprintf(cursor_size, sizeof(cursor_size), "%u", server.config->cursor_size);
  cursor_size[7] = 0;

  setenv("XCURSOR_THEME", server.config->cursor_theme, true);
  setenv("XCURSOR_SIZE", cursor_size, true);

  server.cursor_mode = MWC_CURSOR_PASSTHROUGH;
  server.cursor_motion.notify = server_handle_cursor_motion;
  wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
  server.cursor_motion_absolute.notify = server_handle_cursor_motion_absolute;
  wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);
  server.cursor_button.notify = server_handle_cursor_button;
  wl_signal_add(&server.cursor->events.button, &server.cursor_button);
  server.cursor_axis.notify = server_handle_cursor_axis;
  wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
  server.cursor_frame.notify = server_handle_cursor_frame;
  wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

  /*
   * Configures a seat, which is a single "seat" at which a user sits and
   * operates the computer. This conceptually includes up to one keyboard,
   * pointer, touch, and drawing tablet device. We also rig up a listener to
   * let us know when new input devices are available on the backend.
   */
  wl_list_init(&server.keyboards);
  server.new_input.notify = server_handle_new_input;
  wl_signal_add(&server.backend->events.new_input, &server.new_input);

  server.seat = wlr_seat_create(server.wl_display, "seat0");
  server.request_cursor.notify = server_handle_request_cursor;
  wl_signal_add(&server.seat->events.request_set_cursor,
                &server.request_cursor);
  server.request_set_selection.notify = server_handle_request_set_selection;
  wl_signal_add(&server.seat->events.request_set_selection,
                &server.request_set_selection);

  server.drag_icon_tree = wlr_scene_tree_create(&server.scene->tree);
	wlr_scene_node_set_enabled(&server.drag_icon_tree->node, false);

	server.request_drag.notify = server_handle_request_drag;
	wl_signal_add(&server.seat->events.request_start_drag, &server.request_drag);

	server.request_start_drag.notify = server_handle_request_start_drag;
	wl_signal_add(&server.seat->events.start_drag, &server.request_start_drag);

	server.request_destroy_drag.notify = server_handle_destroy_drag;

  /* handles clipboard clients */
  wlr_data_control_manager_v1_create(server.wl_display);

  /* configures decorations */
  server.xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(server.wl_display);

  server.request_xdg_decoration.notify = server_handle_request_xdg_decoration;
  wl_signal_add(&server.xdg_decoration_manager->events.new_toplevel_decoration,
                &server.request_xdg_decoration);

  wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);
  wlr_viewporter_create(server.wl_display);
	wlr_presentation_create(server.wl_display, server.backend);

  wlr_server_decoration_manager_set_default_mode(wlr_server_decoration_manager_create(server.wl_display),
                                                 server.config->client_side_decorations
                                                 ? WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT
                                                 : WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

  wlr_screencopy_manager_v1_create(server.wl_display);
  wlr_export_dmabuf_manager_v1_create(server.wl_display);
  server.foreign_toplevel_manager = wlr_foreign_toplevel_manager_v1_create(server.wl_display);

  wlr_fractional_scale_manager_v1_create(server.wl_display, 1);

  wlr_virtual_pointer_manager_v1_create(server.wl_display);
  wlr_virtual_keyboard_manager_v1_create(server.wl_display);

  server.gamma_control_manager = wlr_gamma_control_manager_v1_create(server.wl_display);
  server.set_gamma.notify = gamma_control_set_gamma;
  wl_signal_add(&server.gamma_control_manager->events.set_gamma, &server.set_gamma);

  server.session_lock_manager = wlr_session_lock_manager_v1_create(server.wl_display);
  server.new_lock.notify = session_lock_manager_handle_new;
  server.lock_manager_destroy.notify = session_lock_manager_handle_destroy;
  wl_signal_add(&server.session_lock_manager->events.new_lock, &server.new_lock);
  wl_signal_add(&server.session_lock_manager->events.destroy, &server.lock_manager_destroy);

  server.cursor_shape_manager = wlr_cursor_shape_manager_v1_create(server.wl_display, 1);
  server.request_cursor_shape.notify = server_handle_request_cursor_shape;
  server.cursor_shape_manager_destroy.notify = server_handle_cursor_shape_destroy;
  wl_signal_add(&server.cursor_shape_manager->events.request_set_shape, &server.request_cursor_shape);
  wl_signal_add(&server.cursor_shape_manager->events.destroy, &server.cursor_shape_manager_destroy);

  /* Add a Unix socket to the Wayland display. */
  const char *socket = wl_display_add_socket_auto(server.wl_display);
  if(!socket) {
    wlr_backend_destroy(server.backend);
    return 1;
  }

  /* Start the backend. This will enumerate outputs and inputs, become the DRM
   * master, etc */
  if(!wlr_backend_start(server.backend)) {
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.wl_display);
    return 1;
  }

  /* Set the WAYLAND_DISPLAY environment variable to our socket */
  setenv("WAYLAND_DISPLAY", socket, true);

  /* creating a thread for the ipc to run on */
  pthread_t thread_id;
  pthread_create(&thread_id, NULL, run_ipc, NULL);

  /* sleep a bit so the ipc starts, 0.1 seconds is probably enough */
  usleep(100000);

  for(size_t i = 0; i < server.config->run_count; i++) {
    run_cmd(server.config->run[i]);
  }

  server.running = true;

  /* run the wayland event loop. */
  wlr_log(WLR_INFO, "running mwc on WAYLAND_DISPLAY=%s", socket);
  wl_display_run(server.wl_display);

  /* Once wl_display_run returns, we destroy all clients then shut down the
   * server. */
  wl_display_destroy_clients(server.wl_display);
  wlr_scene_node_destroy(&server.scene->tree.node);
  wlr_xcursor_manager_destroy(server.cursor_mgr);
  wlr_cursor_destroy(server.cursor);
  wlr_allocator_destroy(server.allocator);
  wlr_renderer_destroy(server.renderer);
  wlr_backend_destroy(server.backend);
  wl_display_destroy(server.wl_display);

  return 0;
}
