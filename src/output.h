#pragma once

#include <scenefx/types/wlr_scene.h>

#include <wlr/types/wlr_output.h>

#include "workspace.h"
#include "notwc.h"

struct notwc_output {
	struct wl_list link;
	struct wlr_output *wlr_output;
  struct wl_list workspaces;
  struct wlr_box usable_area;

  struct {
    struct wl_list background;
    struct wl_list bottom;
    struct wl_list top;
    struct wl_list overlay;
  } layers;

  struct wlr_scene_optimized_blur *blur;

  struct notwc_workspace *active_workspace;

  struct wlr_scene_rect *session_lock_rect;

	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

void
server_handle_new_output(struct wl_listener *listener, void *data);

bool
output_initialize(struct wlr_output *output, struct output_config *config);

bool
output_transfer_existing_workspaces(struct notwc_output *output);

struct notwc_workspace *
output_find_owned_workspace(struct notwc_output *output);

bool
output_apply_preffered_mode(struct wlr_output *wlr_output, struct wlr_output_state *state);

double
output_frame_duration_ms(struct notwc_output *output);

struct notwc_output *
output_get_relative(struct notwc_output *output, enum notwc_direction direction);

void
cursor_jump_output(struct notwc_output *output);

void
focus_output(struct notwc_output *output,
             enum notwc_direction side);

void
output_handle_frame(struct wl_listener *listener, void *data);

void
output_handle_request_state(struct wl_listener *listener, void *data);

void
output_handle_destroy(struct wl_listener *listener, void *data);

void
output_move_workspaces(struct notwc_output *dest, struct notwc_output *src);
