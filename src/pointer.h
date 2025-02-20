#pragma once

#include <wlr/types/wlr_input_device.h>
#include <libinput.h>

enum mwc_cursor_mode {
	MWC_CURSOR_PASSTHROUGH,
	MWC_CURSOR_MOVE,
	MWC_CURSOR_RESIZE,
};

void
server_handle_new_pointer(struct wlr_input_device *device);

void
pointer_device_configure(struct libinput_device *device);

void
server_reset_cursor_mode(void);

void
cursor_handle_motion(uint32_t time);

void
server_handle_cursor_motion(struct wl_listener *listener, void *data);

void
server_handle_cursor_motion_absolute(struct wl_listener *listener, void *data);

void
server_handle_cursor_button(struct wl_listener *listener, void *data);

void
server_handle_cursor_axis(struct wl_listener *listener, void *data);

void
server_handle_cursor_frame(struct wl_listener *listener, void *data);
