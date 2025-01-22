#pragma once

#include <wlr/types/wlr_session_lock_v1.h>

struct owl_lock {
  struct wlr_session_lock_v1 *wlr_lock;
  bool locked;

  struct wlr_scene_rect *rect;

  struct wl_listener new_surface;
  struct wl_listener unlock;
  struct wl_listener destroy;
};

void
session_lock_manager_handle_new(struct wl_listener *listener, void *data);

void
session_lock_manager_handle_destroy(struct wl_listener *listener, void *data);
