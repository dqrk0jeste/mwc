#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/fx/corner_location.h>
#include <scenefx/types/wlr_scene.h>

#include "rendering.h"

#include "helpers.h"
#include "mwc.h"
#include "config.h"
#include "something.h"
#include "toplevel.h"
#include "config.h"
#include "workspace.h"

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <wayland-util.h>
#include <wlr/util/log.h>
#include <wlr/util/box.h>

extern struct mwc_server server;

void
toplevel_draw_borders(struct mwc_toplevel *toplevel) {
  uint32_t border_width = server.config->border_width;
  uint32_t border_radius = server.config->border_radius;
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
    wlr_scene_rect_set_corner_radius(toplevel->border, border_radius, border_radius_location);
  }

  wlr_scene_rect_set_size(toplevel->border, width + 2 * border_width,
                          height + 2 * border_width);

  struct clipped_region clipped_region = {
    .area = { border_width, border_width, width, height },
    .corner_radius = max(border_radius - border_width, 0),
    .corners = border_radius_location,
  };
  wlr_scene_rect_set_clipped_region(toplevel->border, clipped_region);

  wlr_scene_rect_set_color(toplevel->border, border_color);
}

void
scene_buffer_apply_effects(struct wlr_scene_buffer *buffer,
                           uint32_t width, uint32_t height,
                           uint32_t geometry_width, uint32_t geometry_height,
                           double opacity, uint32_t border_radius, bool is_popup) {
  wlr_scene_buffer_set_opacity(buffer, opacity);

  /* we dont blur or round popups */
  if(is_popup) return;

  /* some clients, notably firefox, have basically everything as subsurfaces.
   * this is a hacky way that will (usually) detect things that are not a main window and skip them */
  if(buffer->buffer == NULL) return;

  uint32_t buffer_width = buffer->buffer->width;
  uint32_t buffer_height = buffer->buffer->height;

  if(buffer_width < 0.75 * geometry_width || buffer_height < 0.75 * geometry_height) return;

  if(server.config->blur) {
    wlr_scene_buffer_set_backdrop_blur(buffer, true);
    wlr_scene_buffer_set_backdrop_blur_optimized(buffer, true);
    wlr_scene_buffer_set_backdrop_blur_ignore_transparent(buffer, true);
  } else {
    wlr_scene_buffer_set_backdrop_blur(buffer, false);
  }

  wlr_scene_buffer_set_corner_radius(buffer, border_radius,
                                     server.config->border_radius_location);

  wlr_scene_buffer_set_dest_size(buffer, width, height);
}

void
scene_tree_apply_effects(struct wlr_scene_tree *tree,
                         uint32_t width, uint32_t height,
                         uint32_t geometry_width, uint32_t geometry_height,
                         double opacity, uint32_t border_radius, bool is_popup) {
  struct wlr_scene_node *node;
  wl_list_for_each(node, &tree->children, link) {
    switch(node->type) {
      case WLR_SCENE_NODE_BUFFER: {
        struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
        scene_buffer_apply_effects(buffer, width, height, geometry_width, geometry_height,
                                   opacity, border_radius, is_popup);
        break;
      }
      case WLR_SCENE_NODE_TREE: {
        struct wlr_scene_tree *t = wlr_scene_tree_from_node(node);
        struct mwc_something *something = t->node.data;
        if(something != NULL) {
          is_popup = something->type == MWC_POPUP;
        }
        scene_tree_apply_effects(t, width, height, geometry_width, geometry_height,
                                 opacity, border_radius, is_popup);
        break;
      }
      default: break;
    }
  }
}

void
toplevel_apply_effects(struct mwc_toplevel *toplevel) {
  double opacity;
  if(!toplevel->fullscreen || server.config->apply_opacity_when_fullscreen) {
    opacity = toplevel == server.focused_toplevel
      ? toplevel->active_opacity
      : toplevel->inactive_opacity;
  } else {
    opacity = 1.0;
  }

  uint32_t border_radius = toplevel->fullscreen
    ? 0
    : max(server.config->border_radius - server.config->border_width, 0);

  uint32_t width, height;
  toplevel_get_actual_size(toplevel, &width, &height);
  struct wlr_box geometry = toplevel_get_geometry(toplevel);
  
  scene_tree_apply_effects(toplevel->scene_tree, width, height, geometry.width, geometry.height,
                           opacity, border_radius, false);
}

void
toplevel_apply_clip(struct mwc_toplevel *toplevel) {
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
    struct mwc_something *view = n->data;
    if(view != NULL && view->type == MWC_POPUP) {
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
toplevel_animation_next_tick(struct mwc_toplevel *toplevel) {
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

void
toplevel_draw_shadow(struct mwc_toplevel *toplevel) {
  if(toplevel->shadow != NULL && toplevel->fullscreen) {
    wlr_scene_node_set_enabled(&toplevel->shadow->node, false);
    return;
  }

  uint32_t width, height;
  toplevel_get_actual_size(toplevel, &width, &height);

  uint32_t delta = server.config->shadows_size + server.config->border_width;

  /* we calculate where to clip the shadow */
  struct wlr_box toplevel_box = {
    .x = 0,
    .y = 0,
    .width = width,
    .height = height,
  };

  struct wlr_box shadow_box = {
    .x = server.config->shadows_position.x,
    .y = server.config->shadows_position.y,
    .width = width + 2 * delta,
    .height = height + 2 *delta,
  };

  struct wlr_box intersection_box;
  wlr_box_intersection(&intersection_box, &toplevel_box, &shadow_box);
  /* clipped region takes shadow relative coords, so we translate everything by its position */
  intersection_box.x -= server.config->shadows_position.x;
  intersection_box.y -= server.config->shadows_position.y;

  struct clipped_region clipped_region = {
    .area = intersection_box,
    .corner_radius = server.config->border_radius,
    .corners = server.config->border_radius_location,
  };

  if(toplevel->shadow == NULL) {
    toplevel->shadow = wlr_scene_shadow_create(toplevel->scene_tree,
                                               shadow_box.width, shadow_box.height,
                                               server.config->border_radius,
                                               server.config->shadows_blur,
                                               server.config->shadows_color);
    wlr_scene_node_lower_to_bottom(&toplevel->shadow->node);
    wlr_scene_node_set_position(&toplevel->shadow->node, shadow_box.x, shadow_box.y);
  }

  wlr_scene_node_set_enabled(&toplevel->shadow->node, true);
  wlr_scene_shadow_set_size(toplevel->shadow, shadow_box.width, shadow_box.height);
  wlr_scene_shadow_set_clipped_region(toplevel->shadow, clipped_region);
}

bool
toplevel_draw_frame(struct mwc_toplevel *toplevel) {
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
  if(server.config->shadows) {
    toplevel_draw_shadow(toplevel);
  }
  toplevel_apply_clip(toplevel);
  toplevel_apply_effects(toplevel);

  return need_more_frames;
}

void
workspace_draw_frame(struct mwc_workspace *workspace) {
  if(server.grabbed_toplevel != NULL) {
    toplevel_draw_frame(server.grabbed_toplevel);
  }

  bool need_more_frames = false;
  struct mwc_toplevel *t;
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
