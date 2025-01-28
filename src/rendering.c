#include <scenefx/types/fx/corner_location.h>
#include <scenefx/types/wlr_scene.h>

#include "rendering.h"

#include "helpers.h"
#include "owl.h"
#include "config.h"
#include "something.h"
#include "toplevel.h"
#include "config.h"
#include "workspace.h"

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <wlr/util/log.h>

extern struct owl_server server;

void
toplevel_draw_borders(struct owl_toplevel *toplevel) {
  uint32_t border_width = server.config->border_width;
  uint32_t border_radius = max(server.config->border_radius, 1);
  enum corner_location border_radius_location = server.config->border_radius_location;

  float *border_color = toplevel->fullscreen
    ? (float[4]){ 0, 0, 0, 0 }
    : toplevel == server.focused_toplevel
      ? server.config->active_border_color
      : server.config->inactive_border_color;

  uint32_t width, height;
  toplevel_get_actual_size(toplevel, &width, &height);

  if(toplevel->border == NULL) {
    toplevel->border = wlr_scene_rect_create(toplevel->scene_tree, 0, 0, border_color);
    wlr_scene_node_lower_to_bottom(&toplevel->border->node);
    wlr_scene_node_set_position(&toplevel->border->node, -border_width, -border_width);
  }

  wlr_scene_rect_set_size(toplevel->border, width + 2 * border_width,
                          height + 2 * border_width);
  wlr_scene_rect_set_corner_radius(toplevel->border, border_radius, border_radius_location);

  struct clipped_region clipped_region = {
    .area = { 0, 0, width, height },
    .corner_radius = border_radius,
    .corners = border_radius_location,
  };
  wlr_scene_rect_set_clipped_region(toplevel->border, clipped_region);

  wlr_scene_rect_set_color(toplevel->border, border_color);
}

struct owl_something *
scene_tree_get_something(struct wlr_scene_tree *tree, 
                         struct wlr_scene_tree *up_to) {
  struct owl_something *something = tree->node.data;
  if(something != NULL) return something;

  if(tree->node.parent == tree) return NULL;

  return scene_tree_get_something(tree->node.parent, up_to);
}

struct owl_something *
scene_buffer_get_something(struct wlr_scene_buffer *buffer,
                           struct wlr_scene_tree *up_to) {
  return scene_tree_get_something(buffer->node.parent, up_to);
}

struct iter_scene_buffer_apply_effects_args {
  double opacity;
  struct wlr_scene_tree *toplevel_tree;
};

/* this is bad, as it climbs up and down the tree a lot, but will do for testing */
void
iter_scene_buffer_apply_effects(struct wlr_scene_buffer *buffer,
                           int sx, int sy, void *data) {
  struct iter_scene_buffer_apply_effects_args *args = data;
  struct owl_something *something = scene_buffer_get_something(buffer, args->toplevel_tree);
  assert(something != NULL);

  wlr_scene_buffer_set_opacity(buffer, args->opacity);

  /* we dont blur or round popups */
  if(something->type == OWL_POPUP) return;

  if(server.config->blur) {
    wlr_scene_buffer_set_backdrop_blur(buffer, true);
    wlr_scene_buffer_set_backdrop_blur_optimized(buffer, true);
    wlr_scene_buffer_set_backdrop_blur_ignore_transparent(buffer, true);
  }

  if(server.config->border_radius > 0) {
    wlr_scene_buffer_set_corner_radius(buffer, server.config->border_radius,
                                       server.config->border_radius_location);
  }
}

void
toplevel_buffer_apply_effects(struct owl_toplevel *toplevel) {
  struct iter_scene_buffer_apply_effects_args args = {
    .opacity = toplevel->fullscreen
      ? 1.0 : toplevel == server.focused_toplevel
        ? toplevel->active_opacity
        : toplevel->inactive_opacity,
  };

  wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node,
                                 iter_scene_buffer_apply_effects, &args);
}

void
toplevel_apply_clip(struct owl_toplevel *toplevel) {
  uint32_t width, height;
  toplevel_get_actual_size(toplevel, &width, &height);

  struct wlr_box geometry = toplevel_get_geometry(toplevel);
  struct wlr_box clip_box = (struct wlr_box){
    .x = geometry.x,
    .y = geometry.y,
    .width = width,
    .height = height,
  };

  wlr_scene_subsurface_tree_set_clip(&toplevel->scene_tree->node, &clip_box);

  struct wlr_scene_node *n;
  wl_list_for_each(n, &toplevel->scene_tree->children, link) {
    struct owl_something *view = n->data;
    if(view != NULL && view->type == OWL_POPUP) {
      wlr_scene_subsurface_tree_set_clip(n, NULL);
    }
  }
}

double
find_animation_curve_at(double t) {
  size_t down = 0;
  size_t up = BAKED_POINTS_COUNT - 1;

  size_t middle = (up + down) / 2;
  while(up - down != 1) {
    if(server.config->baked_points[middle].x <= t) {
      down = middle;  
    } else {
      up = middle;
    }
    middle = (up + down) / 2;
  }

  return server.config->baked_points[up].y;
}

bool
toplevel_animation_next_tick(struct owl_toplevel *toplevel) {
  double animation_passed =
    (double)toplevel->animation.passed_frames / toplevel->animation.total_frames;
  double factor = find_animation_curve_at(animation_passed);

  uint32_t width = toplevel->animation.initial.width +
    (toplevel->current.width - toplevel->animation.initial.width) * factor;
  uint32_t height = toplevel->animation.initial.height +
    (toplevel->current.height - toplevel->animation.initial.height) * factor;

  uint32_t x = toplevel->animation.initial.x +
    (toplevel->current.x - toplevel->animation.initial.x) * factor;
  uint32_t y = toplevel->animation.initial.y +
    (toplevel->current.y - toplevel->animation.initial.y) * factor;

  wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);

  toplevel->animation.current = (struct wlr_box){
    .x = x,
    .y = y,
    .width = width,
    .height = height,
  };

  if(animation_passed >= 1.0) {
    toplevel->animation.running = false;
    return false;
  } else {
    toplevel->animation.passed_frames++;
    return true;
  }
}

bool
toplevel_draw_frame(struct owl_toplevel *toplevel) {
  if(!toplevel->mapped) return false;
  wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);

  bool need_more_frames = false;
  if(toplevel->animation.running) {
    if(toplevel_animation_next_tick(toplevel)) {
      need_more_frames = true;
    }
  } else {
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                toplevel->current.x, toplevel->current.y);
  }

  toplevel_draw_borders(toplevel);
  toplevel_apply_clip(toplevel);
  toplevel_buffer_apply_effects(toplevel);

  return need_more_frames;
}

void
workspace_draw_frame(struct owl_workspace *workspace) {
  bool need_more_frames = false;

  struct owl_toplevel *t;
  if(workspace->fullscreen_toplevel != NULL) {
    if(toplevel_draw_frame(workspace->fullscreen_toplevel)) {
      need_more_frames = true;
    }
  } else {
    wl_list_for_each(t, &workspace->floating_toplevels, link) {
      if(toplevel_draw_frame(t)) {
        need_more_frames = true;
      }
    }
    wl_list_for_each(t, &workspace->masters, link) {
      if(toplevel_draw_frame(t)) {
        need_more_frames = true;
      }
    }
    wl_list_for_each(t, &workspace->slaves, link) {
      if(toplevel_draw_frame(t)) {
        need_more_frames = true;
      }
    }
  }

  /* if there are animation that are not finished we request more frames
   * for the output, until all the animations are done */
  if(need_more_frames) {
    wlr_output_schedule_frame(workspace->output->wlr_output);
  }
}
