#pragma once

#include <wlr/types/wlr_compositor.h>

enum notwc_type {
  NOTWC_TOPLEVEL,
  NOTWC_POPUP,
  NOTWC_LAYER_SURFACE,
  NOTWC_LOCK_SURFACE,
};

struct notwc_toplevel;
struct notwc_layer_surface;
struct notwc_lock_surface;

struct notwc_something {
  enum notwc_type type;
  union {
    struct notwc_toplevel *toplevel;
    struct notwc_popup *popup;
    struct notwc_layer_surface *layer_surface;
    struct notwc_lock_surface *lock_surface;
  };
};

struct notwc_something *
root_parent_of_surface(struct wlr_surface *wlr_surface);

struct notwc_something *
something_at(double lx, double ly,
             struct wlr_surface **surface,
             double *sx, double *sy);
