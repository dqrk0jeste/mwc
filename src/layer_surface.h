#pragma once

#include "output.h"
#include "something.h"

#include <stdbool.h>
#include <wlr/types/wlr_layer_shell_v1.h>

struct mwc_layer_surface {
  struct wl_list link;
  struct wlr_layer_surface_v1 *wlr_layer_surface;
  struct wlr_scene_layer_surface_v1 *scene;

  struct mwc_something something;

  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener commit;
  struct wl_listener new_popup;
  struct wl_listener destroy;
};

void
server_handle_new_layer_surface(struct wl_listener *listener, void *data);

void
layer_surface_handle_commit(struct wl_listener *listener, void *data);

void
layer_surface_handle_map(struct wl_listener *listener, void *data);

void
layer_surface_handle_unmap(struct wl_listener *listener, void *data);

void
layer_surface_handle_destroy(struct wl_listener *listener, void *data);

void
layer_surface_handle_new_popup(struct wl_listener *listener, void *data);

void
layer_surfaces_commit(struct mwc_output *output);

void
focus_layer_surface(struct mwc_layer_surface *layer_surface);

struct wlr_scene_tree *
layer_get_scene(enum zwlr_layer_shell_v1_layer layer);

struct wl_list *
layer_get_list(struct mwc_output *output, enum zwlr_layer_shell_v1_layer layer);

void
layers_under_fullscreen_set_enabled(struct mwc_output *output, bool enable);

void
iter_scene_buffer_apply_blur(struct wlr_scene_buffer *buffer,
                             int sx, int sy, void *data);
