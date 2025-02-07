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

#include <libinput.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/log.h>

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

  if(server.client_cursor.surface != NULL) {
    wlr_cursor_set_surface(server.cursor, server.client_cursor.surface,
                           server.client_cursor.hotspot_x, server.client_cursor.hotspot_y);
  } else {
    wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "default");
  }
}

void
cursor_handle_motion(uint32_t time) {
  /* get the output that the cursor is on currently */
  struct wlr_output *wlr_output = wlr_output_layout_output_at(
    server.output_layout, server.cursor->x, server.cursor->y);
  struct mwc_output *output = wlr_output->data;

  /* set global active workspace */
  if(output->active_workspace != server.active_workspace) {
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

  /* find something under the pointer and send the event along. */
  double sx, sy;
  struct wlr_seat *seat = server.seat;
  struct wlr_surface *surface = NULL;
  struct mwc_something *something =
    something_at(server.cursor->x, server.cursor->y, &surface, &sx, &sy);

  if(something == NULL) {
    wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "default");
    /* clear pointer focus so future button events and such are not sent to
     * the last client to have the cursor over it */
    wlr_seat_pointer_clear_focus(seat);
    return;
  }

  if(something->type == MWC_TOPLEVEL) {
    focus_toplevel(something->toplevel);
  } else if(something->type == MWC_LAYER_SURFACE){
    focus_layer_surface(something->layer_surface);
  } else if(something->type == MWC_LOCK_SURFACE) {
    focus_lock_surface(something->lock_surface);
  }

  wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
  wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

void
server_handle_cursor_motion(struct wl_listener *listener, void *data) {
  struct wlr_pointer_motion_event *event = data;
  wlr_cursor_move(server.cursor, &event->pointer->base,
                  event->delta_x, event->delta_y);
  cursor_handle_motion(event->time_msec);
}

void
server_handle_cursor_motion_absolute(
  struct wl_listener *listener, void *data) {
  struct wlr_pointer_motion_absolute_event *event = data;
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

