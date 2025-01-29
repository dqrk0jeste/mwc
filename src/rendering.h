#pragma once

#include <stdint.h>
#include <wlr/util/box.h>
#include <wlr/types/wlr_scene.h>

enum notwc_border_state {
  NOTWC_BORDER_INVISIBLE,
  NOTWC_BORDER_ACTIVE,
  NOTWC_BORDER_INACTIVE,
};

struct notwc_animation {
  bool should_animate;
  bool running;
  uint32_t total_frames;
  uint32_t passed_frames;
  struct wlr_box initial;
  struct wlr_box current;
};


double
find_animation_curve_at(double t);

struct notwc_toplevel;

void
toplevel_draw_borders(struct notwc_toplevel *toplevel);

void
toplevel_draw_shadow(struct notwc_toplevel *toplevel);

void
toplevel_draw_placeholder(struct notwc_toplevel *toplevel);

double
calculate_animation_passed(struct notwc_animation *animation);

bool
toplevel_animation_next_tick(struct notwc_toplevel *toplevel);

bool
toplevel_draw_frame(struct notwc_toplevel *toplevel);

void
toplevel_apply_clip(struct notwc_toplevel *toplevel);

void
toplevel_unclip_size(struct notwc_toplevel *toplevel);

struct notwc_workspace;

void
workspace_draw_frame(struct notwc_workspace *workspace);

void
scene_buffer_apply_opacity(struct wlr_scene_buffer *buffer,
                           int sx, int sy, void *user_data);

void
toplevel_handle_opacity(struct notwc_toplevel *toplevel);

void
workspace_handle_opacity(struct notwc_workspace *workspace);
