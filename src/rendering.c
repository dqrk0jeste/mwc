#include "rendering.h"

#include "owl.h"
#include "config.h"
#include "toplevel.h"
#include "workspace.h"

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

extern struct owl_server server;

void
toplevel_draw_borders(struct owl_toplevel *toplevel, uint32_t width, uint32_t height) {
  uint32_t border_width = server.config->border_width;

  float *border_color = toplevel->fullscreen
    ? (float[4]){ 0, 0, 0, 0}
    : toplevel == server.focused_toplevel
      ? server.config->active_border_color
      : server.config->inactive_border_color;
  
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

struct wlr_scene_buffer *
surface_find_buffer(struct wlr_scene_node *node, struct wlr_surface *surface) {
  if(node->type == WLR_SCENE_NODE_BUFFER) {
    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);

    struct wlr_scene_surface *scene_surface =
      wlr_scene_surface_try_from_buffer(scene_buffer);
    if(!scene_surface) {
      return NULL;
    }

    struct wlr_surface *s = scene_surface->surface;

    if(s && s == surface) {
      return scene_buffer;
    }
  }

  if(node->type == WLR_SCENE_NODE_TREE) {
    struct wlr_scene_tree *scene_tree = wlr_scene_tree_from_node(node);

    struct wlr_scene_node *child;
    wl_list_for_each(child, &scene_tree->children, link) {
      struct wlr_scene_buffer *found_buffer = surface_find_buffer(child, surface);
      if(found_buffer) {
        return found_buffer;
      }
    }
  }

  return NULL;
}

double
calculate_animation_curve_at(double x) {
  double a = server.config->animation_curve[0];
  double b = server.config->animation_curve[1];
  double c = server.config->animation_curve[2];

  return (a * x * x * x + b * x * x + c * x) / (a + b + c);
}

double
calculate_animation_passed(struct owl_animation *animation) {
  double passed = (double)animation->passed_frames / animation->total_frames;

  return calculate_animation_curve_at(passed);
}

bool
toplevel_animation_next_tick(struct owl_toplevel *toplevel) {
  double animation_passed = calculate_animation_passed(&toplevel->animation);

  uint32_t width = toplevel->animation.initial.width +
    (toplevel->current.width - toplevel->animation.initial.width) * animation_passed;
  uint32_t height = toplevel->animation.initial.height +
    (toplevel->current.height - toplevel->animation.initial.height) * animation_passed;

  if(width > toplevel->current.width || height > toplevel->current.height) {
    struct wlr_scene_buffer *scene_buffer =
      surface_find_buffer(&toplevel->scene_tree->node,
                          toplevel->xdg_toplevel->base->surface);
    wlr_scene_buffer_set_dest_size(scene_buffer, width, height);
  } else {
    toplevel_clip_to_size(toplevel, width, height); 
  }

  uint32_t x = toplevel->animation.initial.x +
    (toplevel->current.x - toplevel->animation.initial.x) * animation_passed;
  uint32_t y = toplevel->animation.initial.y +
    (toplevel->current.y - toplevel->animation.initial.y) * animation_passed;

  wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
  toplevel_draw_borders(toplevel, width, height);

  toplevel->animation.current = (struct wlr_box){
    .x = x,
    .y = y,
    .width = width,
    .height = height,
  };

  return animation_passed == 1.0;
}

bool toplevel_draw_animation_frame(struct owl_toplevel *toplevel) {
  bool done = toplevel_animation_next_tick(toplevel);
  if(done) {
    toplevel->animation.running = false;
    if(toplevel->floating) {
      toplevel_unclip_size(toplevel);
    }
  } else {
    toplevel->animation.passed_frames++;
  }

  return done;
}

void layout_render_dirty(struct owl_workspace *workspace) {
  struct owl_toplevel *t;
  wl_list_for_each(t, &workspace->masters, link) {
    if(!t->mapped) continue;
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
    struct wlr_scene_buffer *scene_buffer = surface_find_buffer(&t->scene_tree->node,
                                                                t->xdg_toplevel->base->surface);
    wlr_scene_buffer_set_dest_size(scene_buffer, t->current.width, t->current.height);
  }

  wl_list_for_each(t, &workspace->slaves, link) {
    if(!t->mapped) continue;
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
    struct wlr_scene_buffer *scene_buffer = surface_find_buffer(&t->scene_tree->node,
                                                                t->xdg_toplevel->base->surface);
    wlr_scene_buffer_set_dest_size(scene_buffer, t->current.width, t->current.height);
  }
}

void workspace_render_frame(struct owl_workspace *workspace) {
  bool animations_done = true;
  struct owl_toplevel *t;
  wl_list_for_each(t, &workspace->floating_toplevels, link) {
    if(!t->mapped) continue;
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
    if(t->animation.running) {
      bool done = toplevel_draw_animation_frame(t);
      if(!done) {
        animations_done = false;
      }
    } else {
      toplevel_draw_borders(t, t->current.width, t->current.height);
    }
  }
  wl_list_for_each(t, &workspace->masters, link) {
    if(!t->mapped) continue;
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
    if(t->animation.running) {
      bool done = toplevel_draw_animation_frame(t);
      if(!done) {
        animations_done = false;
      }
    } else {
      toplevel_draw_borders(t, t->current.width, t->current.height);
    }
  }
  wl_list_for_each(t, &workspace->slaves, link) {
    if(!t->mapped) continue;
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
    if(t->animation.running) {
      bool done = toplevel_draw_animation_frame(t);
      if(!done) {
        animations_done = false;
      }
    } else {
      toplevel_draw_borders(t, t->current.width, t->current.height);
    }
  }

  /* if there are animation that are not finished we request more frames
   * for the output, until all the animations are done */
  if(!animations_done) {
    wlr_output_schedule_frame(workspace->output->wlr_output);
  }
}
