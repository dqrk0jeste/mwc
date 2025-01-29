#include "pointer.h"

#include "config.h"
#include "keybinds.h"
#include "ipc.h"
#include "notwc.h"
#include "toplevel.h"
#include "output.h"
#include "something.h"
#include "dnd.h"
#include "layer_surface.h"

#include <libinput.h>
#include <wayland-util.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/log.h>

extern struct notwc_server server;

void
server_handle_new_pointer(struct wlr_input_device *device) {
  /* enable natural scrolling and tap to click*/
  if(wlr_input_device_is_libinput(device)) {
    struct libinput_device *libinput_device = wlr_libinput_get_device_handle(device);
    libinput_device_ref(libinput_device);
    pointer_device_configure(libinput_device);
    libinput_device_unref(libinput_device);
  }

  wlr_cursor_attach_input_device(server.cursor, device);
}

void
pointer_device_configure(struct libinput_device *device) {
  const char *name = libinput_device_get_name(device);

  enum libinput_config_accel_profile accel;
  double sensitivity;
  /* we configure accelation and sensitivity of the pointer by
   * first looking at specific pointer configurations */
  bool found = false;
  struct pointer_config *p;
  wl_list_for_each(p, &server.config->pointers, link) {
    if(strcmp(p->name, name) == 0) {
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
      wlr_log(WLR_ERROR, "applying sensitivity to device '%s' failed", name);
    }

    if(accel) {
      struct libinput_config_accel *accel_config = libinput_config_accel_create(accel);
      if(libinput_device_config_accel_apply(device, accel_config)
         != LIBINPUT_CONFIG_STATUS_SUCCESS) {
        wlr_log(WLR_ERROR, "applying acceleration profile to device '%s' failed", name);
      }
      libinput_config_accel_destroy(accel_config);
    }
  }

  /* check if trackpad */
  if(libinput_device_config_tap_get_finger_count(device) > 0) {
    /* then apply trackpad specific settings */
    if(libinput_device_config_tap_set_enabled(device, server.config->trackpad_tap_to_click)
       != LIBINPUT_CONFIG_STATUS_SUCCESS) {
      wlr_log(WLR_ERROR, "applying tap to click to device '%s' failed", name);
    }

    if(libinput_device_config_scroll_set_natural_scroll_enabled(device,
       server.config->trackpad_natural_scroll) != LIBINPUT_CONFIG_STATUS_SUCCESS) {
      wlr_log(WLR_ERROR, "applying natural scroll to device '%s' failed", name);
    }

    if(libinput_device_config_scroll_set_method(device, server.config->trackpad_scroll_method)
       != LIBINPUT_CONFIG_STATUS_SUCCESS) {
      wlr_log(WLR_ERROR, "applying scroll method to device '%s' failed", name);
    }

    if(libinput_device_config_dwt_set_enabled(device,
       server.config->trackpad_disable_while_typing) != LIBINPUT_CONFIG_STATUS_SUCCESS) {
      wlr_log(WLR_ERROR, "applying disable while typing to device '%s' failed", name);
    }
  }
}

void
server_reset_cursor_mode() {
  /* reset the cursor mode to passthrough. */
  server.cursor_mode = NOTWC_CURSOR_PASSTHROUGH;
  server.grabbed_toplevel->resizing = false;
  server.grabbed_toplevel = NULL;

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
  struct notwc_output *output = wlr_output->data;

  /* set global active workspace */
  if(output->active_workspace != server.active_workspace) {
    server.active_workspace = output->active_workspace;
    ipc_broadcast_message(IPC_ACTIVE_WORKSPACE);
  }

  if(server.cursor_mode == NOTWC_CURSOR_MOVE) {
    toplevel_move();
    return;
  } else if (server.cursor_mode == NOTWC_CURSOR_RESIZE) {
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
  struct notwc_something *something =
    something_at(server.cursor->x, server.cursor->y, &surface, &sx, &sy);

  if(something == NULL) {
    wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "default");
    /* clear pointer focus so future button events and such are not sent to
     * the last client to have the cursor over it */
    wlr_seat_pointer_clear_focus(seat);
    return;
  }

  if(something->type == NOTWC_TOPLEVEL) {
    focus_toplevel(something->toplevel);
  } else if(something->type == NOTWC_LAYER_SURFACE){
    focus_layer_surface(something->layer_surface);
  } else if(something->type == NOTWC_LOCK_SURFACE) {
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
    && server.cursor_mode != NOTWC_CURSOR_PASSTHROUGH) {
    struct notwc_output *primary_output = 
      toplevel_get_primary_output(server.grabbed_toplevel);

    if(primary_output != server.grabbed_toplevel->workspace->output) {
      server.grabbed_toplevel->workspace = primary_output->active_workspace;
      wl_list_remove(&server.grabbed_toplevel->link);
      wl_list_insert(&primary_output->active_workspace->floating_toplevels,
                     &server.grabbed_toplevel->link);
    }

    server_reset_cursor_mode();
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

