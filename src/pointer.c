#include "pointer.h"

#include "config.h"
#include "keybinds.h"
#include "ipc.h"
#include "layout.h"
#include "mwc.h"
#include "toplevel.h"
#include "output.h"
#include "something.h"
#include "dnd.h"
#include "layer_surface.h"
#include "workspace.h"

#include <bits/time.h>
#include <libinput.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>

extern struct mwc_server server;

void
server_handle_new_pointer(struct wlr_input_device *device) {
  struct mwc_pointer *pointer = calloc(1, sizeof(*pointer));
  pointer->wlr_pointer = wlr_pointer_from_input_device(device);
  pointer->wlr_pointer->data = pointer;

  if(!pointer_configure(pointer)) {
    wlr_log(WLR_ERROR, "could not configure pointer device, not libinput");
  }

  wlr_cursor_attach_input_device(server.cursor, device);

  wl_list_insert(&server.pointers, &pointer->link);

  pointer->destroy.notify = pointer_handle_destroy;
  wl_signal_add(&device->events.destroy, &pointer->destroy);
}

void
pointer_handle_destroy(struct wl_listener *listener, void *data) {
  struct mwc_pointer *pointer = wl_container_of(listener, pointer, destroy);

  wl_list_remove(&pointer->destroy.link);
  wl_list_remove(&pointer->link);

  free(pointer);
}

bool
pointer_configure(struct mwc_pointer *pointer) {
  if(!wlr_input_device_is_libinput(&pointer->wlr_pointer->base)) return false;

  struct libinput_device *device = wlr_libinput_get_device_handle(&pointer->wlr_pointer->base);
  libinput_device_ref(device);
  pointer->name = libinput_device_get_name(device);

  enum libinput_config_accel_profile accel;
  double sensitivity;
  /* we configure accelation and sensitivity of the pointer by
   * first looking at specific pointer configurations */
  bool found = false;
  struct pointer_config *p;
  wl_list_for_each(p, &server.config->pointers, link) {
    if(strcmp(p->name, pointer->name) == 0) {
      accel = p->acceleration;
      sensitivity = p->sensitivity;
      found = true;
      break;
    }
  }

  if(!found) {
    accel = server.config->pointer_acceleration;
    sensitivity = server.config->pointer_sensitivity;
  }

  if(libinput_device_config_accel_is_available(device)) {
    if(libinput_device_config_accel_set_speed(device, sensitivity)
       != LIBINPUT_CONFIG_STATUS_SUCCESS) {
      wlr_log(WLR_ERROR, "applying sensitivity to device '%s' failed", pointer->name);
    }

    if(accel) {
      struct libinput_config_accel *accel_config = libinput_config_accel_create(accel);
      if(libinput_device_config_accel_apply(device, accel_config)
         != LIBINPUT_CONFIG_STATUS_SUCCESS) {
        wlr_log(WLR_ERROR, "applying acceleration profile to device '%s' failed", pointer->name);
      }
      libinput_config_accel_destroy(accel_config);
    }
  }

  /* check if trackpad */
  if(libinput_device_config_tap_get_finger_count(device) > 0) {
    /* then apply trackpad specific settings */
    if(libinput_device_config_tap_set_enabled(device, server.config->trackpad_tap_to_click)
       != LIBINPUT_CONFIG_STATUS_SUCCESS) {
      wlr_log(WLR_ERROR, "applying tap to click to device '%s' failed", pointer->name);
    }

    if(libinput_device_config_scroll_set_natural_scroll_enabled(device,
       server.config->trackpad_natural_scroll) != LIBINPUT_CONFIG_STATUS_SUCCESS) {
      wlr_log(WLR_ERROR, "applying natural scroll to device '%s' failed", pointer->name);
    }

    if(libinput_device_config_scroll_set_method(device, server.config->trackpad_scroll_method)
       != LIBINPUT_CONFIG_STATUS_SUCCESS) {
      wlr_log(WLR_ERROR, "applying scroll method to device '%s' failed", pointer->name);
    }

    if(libinput_device_config_dwt_set_enabled(device,
       server.config->trackpad_disable_while_typing) != LIBINPUT_CONFIG_STATUS_SUCCESS) {
      wlr_log(WLR_ERROR, "applying disable while typing to device '%s' failed", pointer->name);
    }
  }

  libinput_device_unref(device);

  return true;
}

void
server_reset_cursor_mode() {
  /* reset the cursor mode to passthrough. */
  server.cursor_mode = MWC_CURSOR_PASSTHROUGH;
  server.grabbed_toplevel->resizing = false;
  server.grabbed_toplevel = NULL;
  server.client_driven_move_resize = false;

  wlr_seat_pointer_clear_focus(server.seat);

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  pointer_handle_focus(now.tv_sec * 1000 + now.tv_nsec / 1000, false);
}

void
cursor_handle_motion(uint32_t time) {
  /* get the output that the cursor is on currently */
  struct wlr_output *wlr_output = wlr_output_layout_output_at(server.output_layout,
                                                              server.cursor->x, server.cursor->y);
  struct mwc_output *output = wlr_output->data;

  /* set global active workspace and stop moving resizing if there is a fullscreened toplevel */
  if(output->active_workspace != server.active_workspace) {
    struct mwc_workspace *prev_workspace = server.active_workspace;

    if(output->active_workspace->fullscreen_toplevel != NULL) {
      if(server.cursor_mode == MWC_CURSOR_MOVE) {
        if(!server.grabbed_toplevel->floating) {
          toplevel_tiled_insert_into_layout(server.grabbed_toplevel,
                                            server.cursor->x, server.cursor->y);
        } else {
          server.grabbed_toplevel->workspace = prev_workspace;
          wl_list_insert(&prev_workspace->floating_toplevels, &server.grabbed_toplevel->link);
        }

        server_reset_cursor_mode();
        layout_set_pending_state(prev_workspace);
      } else if(server.cursor_mode == MWC_CURSOR_RESIZE) {
        server_reset_cursor_mode();
      }
    }

    server.active_workspace = output->active_workspace;
    ipc_broadcast_message(IPC_ACTIVE_WORKSPACE);
  }

  if(server.cursor_mode == MWC_CURSOR_MOVE) {
    toplevel_move();
    return;
  } else if (server.cursor_mode == MWC_CURSOR_RESIZE) {
    toplevel_resize();
    return;
  }

  if(server.drag_active) {
    dnd_icons_move(server.cursor->x, server.cursor->y);
  }

  pointer_handle_focus(time, true);
}

void
pointer_handle_focus(uint32_t time, bool handle_keyboard_focus) {
  /* find something under the pointer and send the event along. */
  double sx, sy;
  struct wlr_seat *seat = server.seat;
  struct wlr_surface *surface = NULL;
  struct mwc_something *something = something_at(server.cursor->x, server.cursor->y,
                                                 &surface, &sx, &sy);
  if(something == NULL) {
    wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "default");
    /* clear pointer focus so future button events and such are not sent to
     * the last client to have the cursor over it */
    wlr_seat_pointer_clear_focus(seat);
    return;
  }

  if(something->type == MWC_TITLEBAR_CLOSE_BUTTON) {
    wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "pointer");
    wlr_seat_pointer_clear_focus(seat);
  } else if(something->type == MWC_TITLEBAR_BASE) {
    wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "default");
    wlr_seat_pointer_clear_focus(seat);
  }

  if(handle_keyboard_focus) {
    focus_something(something);
  }

  if(surface != NULL) {
    struct wlr_pointer_constraint_v1 *wlr_constraint =
      wlr_pointer_constraints_v1_constraint_for_surface(server.pointer_contrains_manager,
                                                        surface, server.seat);
    if(wlr_constraint == NULL || wlr_constraint->data == NULL) {
      server.current_constraint = NULL;
    } else {
      constraint_set_as_current(wlr_constraint->data);
    }

    wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(seat, time, sx, sy);
  }
}

void
server_handle_cursor_motion(struct wl_listener *listener, void *data) {
  struct wlr_pointer_motion_event *event = data;

  constrain_apply_to_move(&event->delta_x, &event->delta_y);

  wlr_relative_pointer_manager_v1_send_relative_motion(server.relative_pointer_manager,
                                                       server.seat,
                                                       (uint64_t)event->time_msec * 1000,
                                                       event->delta_x, event->delta_y,
                                                       event->unaccel_dx, event->unaccel_dy);

  wlr_cursor_move(server.cursor, &event->pointer->base, event->delta_x, event->delta_y);
  cursor_handle_motion(event->time_msec);
}

void
server_handle_cursor_motion_absolute(struct wl_listener *listener, void *data) {
  struct wlr_pointer_motion_absolute_event *event = data;

  double lx, ly;
	wlr_cursor_absolute_to_layout_coords(server.cursor, &event->pointer->base,
                                       event->x, event->y, &lx, &ly);

	double dx = lx - server.cursor->x;
	double dy = ly - server.cursor->y;

  wlr_relative_pointer_manager_v1_send_relative_motion(server.relative_pointer_manager,
                                                       server.seat,
                                                       (uint64_t)event->time_msec * 1000,
                                                       dx, dy, dx, dy);

  wlr_cursor_warp_absolute(server.cursor, &event->pointer->base, event->x, event->y);
  cursor_handle_motion(event->time_msec);
}

void
server_handle_cursor_button(struct wl_listener *listener, void *data) {
  struct wlr_pointer_button_event *event = data;

  uint32_t modifiers = server.last_used_keyboard
    ? wlr_keyboard_get_modifiers(server.last_used_keyboard->wlr_keyboard)
    : 0;

  struct keybind *k;
  wl_list_for_each(k, &server.config->pointer_keybinds, link) {
    if(!k->initialized) continue;

    if(k->active && k->stop && event->button == k->key
       && event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
      k->active = false;
      k->stop(k->args);
      return;
    }

    if(modifiers == k->modifiers && event->button == k->key
       && event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
      k->active = true;
      k->action(k->args);
      return;
    }
  }

  /* notify the client with pointer focus that a button press has occurred */
  wlr_seat_pointer_notify_button(server.seat, event->time_msec,
                                 event->button, event->state);

  if(event->state == WL_POINTER_BUTTON_STATE_RELEASED
     && server.cursor_mode != MWC_CURSOR_PASSTHROUGH
     && server.client_driven_move_resize) {
    struct mwc_output *primary_output = 
      toplevel_get_primary_output(server.grabbed_toplevel);

    if(primary_output != server.grabbed_toplevel->workspace->output) {
      server.grabbed_toplevel->workspace = primary_output->active_workspace;
      wl_list_remove(&server.grabbed_toplevel->link);
      wl_list_insert(&primary_output->active_workspace->floating_toplevels,
                     &server.grabbed_toplevel->link);
    }

    if(!server.grabbed_toplevel->floating) {
      toplevel_tiled_insert_into_layout(server.grabbed_toplevel, server.cursor->x, server.cursor->y);
    } else {
      wl_list_insert(server.active_workspace->floating_toplevels.next, &server.grabbed_toplevel->link);
    }

    server_reset_cursor_mode();
    layout_set_pending_state(server.active_workspace);
    return;
  }

  struct wlr_surface *surface;
  double sx, sy;
  struct mwc_something *something = something_at(server.cursor->x, server.cursor->y,
                                                 &surface, &sx, &sy);

  if(something->type == MWC_TITLEBAR_CLOSE_BUTTON
     && event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
    struct mwc_toplevel *toplevel = something->rect->node.parent->node.data;
    wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
  }
}


void
server_handle_cursor_axis(struct wl_listener *listener, void *data) {
  struct wlr_pointer_axis_event *event = data;

  /* notify the client with pointer focus of the axis event */
  wlr_seat_pointer_notify_axis(server.seat,
                               event->time_msec, event->orientation, event->delta,
                               event->delta_discrete, event->source, event->relative_direction);
}

void
server_handle_cursor_frame(struct wl_listener *listener, void *data) {
  wlr_seat_pointer_notify_frame(server.seat);
}

/* a lot of the code was stolen of labwc's implemenetation, big props to them */
void
server_handle_new_constraint(struct wl_listener *listener, void *data) {
	struct wlr_pointer_constraint_v1 *wlr_constraint = data;

  /* if there is already a constraint on this surface we ignore it */
  struct wlr_pointer_constraint_v1 *con;
  wl_list_for_each(con, &server.pointer_contrains_manager->constraints, link) {
    if(con != wlr_constraint && con->surface == wlr_constraint->surface) return;
  }

  struct mwc_pointer_constraint *constraint = calloc(1, sizeof(*constraint));
  constraint->wlr_pointer_constraint = wlr_constraint;
  constraint->wlr_pointer_constraint->data = constraint;
  
  constraint->destroy.notify = constraint_handle_destroy;
  wl_signal_add(&wlr_constraint->events.destroy, &constraint->destroy);
}

void
constraint_remove_current(void) {
  if(server.current_constraint == NULL) return;

  constraint_move_to_hint(server.current_constraint);

  server.current_constraint = NULL;
  wlr_pointer_constraint_v1_send_deactivated(server.current_constraint->wlr_pointer_constraint);
}

void
constraint_set_as_current(struct mwc_pointer_constraint *constraint) {
  if(server.current_constraint == constraint) return;

  if(server.current_constraint != NULL) {
    wlr_pointer_constraint_v1_send_deactivated(server.current_constraint->wlr_pointer_constraint);
  }

  server.current_constraint = constraint;
  constraint_move_to_hint(constraint);
  wlr_pointer_constraint_v1_send_activated(constraint->wlr_pointer_constraint);
}

void
constraint_move_to_hint(struct mwc_pointer_constraint *constraint) {
  struct wlr_pointer_constraint_v1 *wlr_constraint = constraint->wlr_pointer_constraint;

  if(wlr_constraint->current.committed & WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT) {
    double sx = wlr_constraint->current.cursor_hint.x;
    double sy = wlr_constraint->current.cursor_hint.y;
    wlr_cursor_warp(server.cursor, NULL,
                    X(server.focused_toplevel) + sx,
                    Y(server.focused_toplevel) + sy);

    /* make sure we are not sending unnecessary surface movements (took from labwc)*/
    wlr_seat_pointer_warp(server.seat, sx, sy);
  }
}

void
constraint_handle_destroy(struct wl_listener *listener, void *data) {
	struct mwc_pointer_constraint *constraint = wl_container_of(listener, constraint, destroy);

	wl_list_remove(&constraint->destroy.link);
	if(server.current_constraint == constraint) {
    constraint_move_to_hint(constraint);
    server.current_constraint = NULL;
	}

	free(constraint);
}

void
constrain_apply_to_move(double *dx, double *dy) {
  if(server.current_constraint == NULL) return;

  if(server.current_constraint->wlr_pointer_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
    *dx = 0;
    *dy = 0;
    return;
  }

  if(server.seat->pointer_state.focused_surface == NULL) return;

	double current_x = server.seat->pointer_state.sx;
	double current_y = server.seat->pointer_state.sy;

  double constrained_x, constrained_y;
  if(wlr_region_confine(&server.current_constraint->wlr_pointer_constraint->region,
                     current_x, current_y,
                     current_x + *dx, current_y + *dy,
                     &constrained_x, &constrained_y)) {
    *dx = constrained_x - current_x;
    *dy = constrained_y - current_y;
  }
}

void
server_handle_relative_pointer_manager_destroy(struct wl_listener *listener, void *data) {
  wl_list_remove(&server.relative_pointer_manager_destroy.link);
}
