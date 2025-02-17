#pragma once

#include <stdint.h>
#include <wlr/util/box.h>
#include <wlr/types/wlr_scene.h>

enum mwc_border_state {
  MWC_BORDER_INVISIBLE,
  MWC_BORDER_ACTIVE,
  MWC_BORDER_INACTIVE,
};

struct mwc_animation {
  bool should_animate;
  bool running;
  uint32_t total_frames;
  uint32_t passed_frames;
  struct wlr_box initial;
  struct wlr_box current;
};


double
find_animation_curve_at(double t);

struct mwc_toplevel;

void
toplevel_draw_borders(struct mwc_toplevel *toplevel);

void
toplevel_draw_shadow(struct mwc_toplevel *toplevel);

void
toplevel_draw_placeholder(struct mwc_toplevel *toplevel);

double
calculate_animation_passed(struct mwc_animation *animation);

bool
toplevel_animation_next_tick(struct mwc_toplevel *toplevel);

bool
toplevel_draw_frame(struct mwc_toplevel *toplevel);

void
toplevel_apply_clip(struct mwc_toplevel *toplevel);

void
toplevel_unclip_size(struct mwc_toplevel *toplevel);

struct mwc_workspace;

void
workspace_draw_frame(struct mwc_workspace *workspace);

void
toplevel_apply_effects(struct mwc_toplevel *toplevel);
