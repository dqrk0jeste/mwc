#include "rendering.h"

#include "helpers.h"
#include "owl.h"
#include "config.h"
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

  float *border_color = toplevel->fullscreen
    ? (float[4]){ 0, 0, 0, 0 }
    : toplevel == server.focused_toplevel
      ? server.config->active_border_color
      : server.config->inactive_border_color;

  uint32_t width, height;
  toplevel_get_actual_size(toplevel, &width, &height);
  
  if(toplevel->borders[0] == NULL) {
    toplevel->borders[0] = wlr_scene_rect_create(toplevel->scene_tree,
                                                 width + 2 * border_width,
                                                 border_width, border_color);
    wlr_scene_node_set_position(&toplevel->borders[0]->node, -border_width, -border_width);

    toplevel->borders[1] = wlr_scene_rect_create(toplevel->scene_tree,
                                                 border_width, height, border_color);
    wlr_scene_node_set_position(&toplevel->borders[1]->node, width, 0);

    toplevel->borders[2] = wlr_scene_rect_create(toplevel->scene_tree,
                                                 width + 2 * border_width,
                                                 border_width, border_color);
    wlr_scene_node_set_position(&toplevel->borders[2]->node, -border_width, height);

    toplevel->borders[3] = wlr_scene_rect_create(toplevel->scene_tree,
                                                 border_width, height, border_color);
    wlr_scene_node_set_position(&toplevel->borders[3]->node, -border_width, 0);
  } else {
    wlr_scene_node_set_position(&toplevel->borders[1]->node, width, 0);
    wlr_scene_node_set_position(&toplevel->borders[2]->node, -border_width, height);

    wlr_scene_rect_set_size(toplevel->borders[0], width + 2 * border_width, border_width);
    wlr_scene_rect_set_size(toplevel->borders[1], border_width, height);
    wlr_scene_rect_set_size(toplevel->borders[2], width + 2 * border_width, border_width);
    wlr_scene_rect_set_size(toplevel->borders[3], border_width, height);
  }

  for(size_t i = 0; i < 4; i++) {
    wlr_scene_rect_set_color(toplevel->borders[i], border_color);
  }
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
  if(toplevel->animation.snapshot != NULL) {
    wlr_scene_subsurface_tree_set_clip(&toplevel->animation.snapshot->node, &clip_box);
  }

  struct wlr_scene_node *n;
  wl_list_for_each(n, &toplevel->scene_tree->children, link) {
    struct owl_something *something = n->data;
    if(something != NULL && something->type == OWL_POPUP) {
      wlr_scene_subsurface_tree_set_clip(n, NULL);
    }
  }
  /*if(toplevel->animation.snapshot != NULL) {*/
  /*  wl_list_for_each(n, &toplevel->animation.snapshot->children, link) {*/
  /*    struct owl_something *view = n->data;*/
  /*    if(view != NULL && view->type == OWL_POPUP) {*/
  /*      wlr_scene_subsurface_tree_set_clip(&toplevel->scene_tree->node, NULL);*/
  /*    }*/
  /*  }*/
  /*}*/
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

double
calculate_animation_passed(struct owl_animation *animation) {
  return (double)animation->passed_frames / animation->total_frames;
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

  if(animation_passed == 1.0) {
    toplevel->animation.running = false;
    wlr_scene_node_destroy(&toplevel->animation.snapshot->node);
    toplevel->animation.snapshot = NULL;
    return false;
  } else {
    toplevel->animation.passed_frames++;
    return true;
  }
}

void
scene_node_snapshot(struct wlr_scene_node *node, int lx, int ly,
                    struct wlr_scene_tree *snapshot_tree) {
  if(!node->enabled && node->type != WLR_SCENE_NODE_TREE) {
    return;
  }

  lx += node->x;
  ly += node->y;

  struct wlr_scene_node *snapshot_node = NULL;

  switch(node->type) {
    case WLR_SCENE_NODE_TREE:;
      struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);

      struct wlr_scene_node *child;
      wl_list_for_each(child, &scene_tree->children, link) {
        scene_node_snapshot(child, lx, ly, snapshot_tree);
      }
      break;
    case WLR_SCENE_NODE_BUFFER:;
      struct wlr_scene_buffer *scene_buffer =
        wlr_scene_buffer_from_node(node);

      struct wlr_scene_buffer *snapshot_buffer =
        wlr_scene_buffer_create(snapshot_tree, NULL);

      snapshot_node = &snapshot_buffer->node;
      snapshot_buffer->node.data = scene_buffer->node.data;

      wlr_scene_buffer_set_dest_size(snapshot_buffer, scene_buffer->dst_width,
                                     scene_buffer->dst_height);
      wlr_scene_buffer_set_opaque_region(snapshot_buffer,
                                         &scene_buffer->opaque_region);
      wlr_scene_buffer_set_source_box(snapshot_buffer,
                                      &scene_buffer->src_box);
      wlr_scene_buffer_set_transform(snapshot_buffer,
                                     scene_buffer->transform);
      wlr_scene_buffer_set_filter_mode(snapshot_buffer,
                                       scene_buffer->filter_mode);

      // Effects
      wlr_scene_buffer_set_opacity(snapshot_buffer, scene_buffer->opacity);
      /*wlr_scene_buffer_set_corner_radius(snapshot_buffer,*/
      /*								   scene_buffer->corner_radius,*/
      /*								   scene_buffer->corners);*/

      /*wlr_scene_buffer_set_backdrop_blur_optimized(*/
      /*	snapshot_buffer, scene_buffer->backdrop_blur_optimized);*/
      /*wlr_scene_buffer_set_backdrop_blur_ignore_transparent(*/
      /*	snapshot_buffer, scene_buffer->backdrop_blur_ignore_transparent);*/
      /*wlr_scene_buffer_set_backdrop_blur(snapshot_buffer,*/
      /*								   scene_buffer->backdrop_blur);*/

      snapshot_buffer->node.data = scene_buffer->node.data;

      struct wlr_scene_surface *scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);
      if(scene_surface != NULL && scene_surface->surface->buffer != NULL) {
        wlr_scene_buffer_set_buffer(snapshot_buffer,
                                    &scene_surface->surface->buffer->base);
      } else {
        wlr_scene_buffer_set_buffer(snapshot_buffer, scene_buffer->buffer);
      }
      break;

    case WLR_SCENE_NODE_RECT:;
      break;
    /*case WLR_SCENE_NODE_OPTIMIZED_BLUR:*/
    /*	break;*/
    /**/
    /*case WLR_SCENE_NODE_SHADOW:;*/
    /*	struct wlr_scene_shadow *scene_shadow =*/
    /*		wlr_scene_shadow_from_node(node);*/
    /**/
    /*	struct wlr_scene_shadow *snapshot_shadow = wlr_scene_shadow_create(*/
    /*		snapshot_tree, scene_shadow->width, scene_shadow->height,*/
    /*		scene_shadow->corner_radius, scene_shadow->blur_sigma,*/
    /*		scene_shadow->color);*/
    /*	if (snapshot_shadow == NULL) {*/
    /*		return false;*/
    /*	}*/
    /*	snapshot_node = &snapshot_shadow->node;*/
    /**/
    /*	snapshot_shadow->node.data = scene_shadow->node.data;*/
    /**/
    /*	break;*/
  }

  if(snapshot_node != NULL) {
    wlr_scene_node_set_position(snapshot_node, lx, ly);
  }
}

struct wlr_scene_tree *
wlr_scene_tree_snapshot(struct wlr_scene_node *node,
                         struct wlr_scene_tree *parent) {
  struct wlr_scene_tree *snapshot = wlr_scene_tree_create(parent);

  /* Disable and enable the snapshot tree like so to atomically update */
  /* the scene-graph. This will prevent over-damaging or other weirdness. */
  wlr_scene_node_set_enabled(&snapshot->node, false);
  scene_node_snapshot(node, 0, 0, snapshot);
  wlr_scene_node_set_enabled(&snapshot->node, true);

  return snapshot;
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
  toplevel_handle_opacity(toplevel);

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

void
scene_buffer_apply_opacity(struct wlr_scene_buffer *buffer,
                           int sx, int sy, void *data) {
  wlr_scene_buffer_set_opacity(buffer, *(double *)data);
}

void
toplevel_handle_opacity(struct owl_toplevel *toplevel) {
  double opacity = toplevel->fullscreen
    ? 1.0
    : toplevel == server.focused_toplevel
      ? toplevel->active_opacity
      : toplevel->inactive_opacity;

  wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node, scene_buffer_apply_opacity, &opacity);
}

void
workspace_handle_opacity(struct owl_workspace *workspace) {
  struct owl_toplevel *t;
  wl_list_for_each(t, &workspace->floating_toplevels, link) {
    if(!t->mapped) continue;
    toplevel_handle_opacity(t);
  }
  wl_list_for_each(t, &workspace->masters, link) {
    if(!t->mapped) continue;
    toplevel_handle_opacity(t);
  }
  wl_list_for_each(t, &workspace->slaves, link) {
    if(!t->mapped) continue;
    toplevel_handle_opacity(t);
  }
}
