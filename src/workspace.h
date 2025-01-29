#pragma once

#include <scenefx/types/wlr_scene.h>

#include "config.h"
#include "toplevel.h"
#include "output.h"

#include <wayland-server-protocol.h>

struct notwc_animation;

struct notwc_workspace {
  struct wl_list link;

  struct notwc_output *output;
  uint32_t index;
  struct workspace_config *config;

  struct wl_list masters;
  struct wl_list slaves;
  struct wl_list floating_toplevels;
  struct notwc_toplevel *fullscreen_toplevel;
};

void
workspace_create_for_output(struct notwc_output *output, struct workspace_config *config);

void
change_workspace(struct notwc_workspace *workspace, bool keep_focus);

void
toplevel_move_to_workspace(struct notwc_toplevel *toplevel, struct notwc_workspace *workspace);
