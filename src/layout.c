#include "layout.h"

#include "mwc.h"
#include "config.h"
#include "toplevel.h"
#include "wlr/util/box.h"

#include <stdint.h>
#include <wayland-util.h>
#include <wlr/types/wlr_scene.h>

extern struct mwc_server server;

void
calculate_masters_container_size(struct mwc_output *output, uint32_t master_count,
                                 uint32_t slave_count, uint32_t *width, uint32_t *height) {
  uint32_t outer_gaps = server.config->outer_gaps;
  uint32_t inner_gaps = server.config->inner_gaps;
  double master_ratio = server.config->master_ratio;

  struct wlr_box output_box = output->usable_area;

  uint32_t total_width = slave_count > 0
    ? output_box.width * master_ratio
    : output_box.width;

  uint32_t total_gaps = slave_count > 0
    ? outer_gaps // left outer gaps
      + (master_count - 1) * 2 * inner_gaps // inner gaps between masters
      + inner_gaps // right inner gaps 
    : outer_gaps // left outer gaps
      + (master_count - 1) * 2 * inner_gaps // inner gaps between masters
      + outer_gaps; // right outer gaps

  *width = (total_width - total_gaps) / master_count;
  *height = output_box.height - 2 * outer_gaps;
}

void
calculate_slaves_container_size(struct mwc_output *output, uint32_t slave_count,
                                 uint32_t *width, uint32_t *height) {
  uint32_t outer_gaps = server.config->outer_gaps;
  uint32_t inner_gaps = server.config->inner_gaps;
  double master_ratio = server.config->master_ratio;

  struct wlr_box output_box = output->usable_area;

  uint32_t total_gaps = outer_gaps // top outer gaps
    + (slave_count - 1) * 2 * inner_gaps // inner gaps between slaves
    + outer_gaps; // bottom outer gaps

  *width = output_box.width * (1 - master_ratio) - outer_gaps - inner_gaps;
  *height = (output_box.height - total_gaps) / slave_count;
}

bool
toplevel_is_master(struct mwc_toplevel *toplevel) {
  struct mwc_toplevel *t;
  wl_list_for_each(t, &toplevel->workspace->masters, link) {
    if(toplevel == t) return true;
  };
  return false;
}

bool
toplevel_is_slave(struct mwc_toplevel *toplevel) {
  struct mwc_toplevel *t;
  wl_list_for_each(t, &toplevel->workspace->slaves, link) {
    if(toplevel == t) return true;
  };
  return false;
}

void
layout_set_pending_state(struct mwc_workspace *workspace) {
  /* if there is a fullscreened toplevel we just skip */
  if(workspace->fullscreen_toplevel != NULL) return;

  /* if there are no masters we are done */
  if(wl_list_empty(&workspace->masters)) return;

  struct mwc_output *output = workspace->output;

  uint32_t outer_gaps = server.config->outer_gaps;
  uint32_t inner_gaps = server.config->inner_gaps;

  uint32_t slave_count = wl_list_length(&workspace->slaves);
  uint32_t master_count = wl_list_length(&workspace->masters);

  uint32_t container_width, container_height, width, height, x, y;
  calculate_masters_container_size(output, master_count, slave_count,
                                   &container_width, &container_height);

  struct mwc_toplevel *toplevel;
  size_t i = 0;
  wl_list_for_each(toplevel, &workspace->masters, link) {
    width = container_width;
    height = container_height;
    toplevel_strip_decorations_of_size(toplevel, &width, &height);

    x = output->usable_area.x + outer_gaps
      + container_width * i
      + inner_gaps * 2 * i;
    y = output->usable_area.y + outer_gaps;
    toplevel_adjust_buffer_start_position(toplevel, &x, &y);

    toplevel_set_pending_state(toplevel, x, y, width, height);
    i++;
  }

  if(slave_count == 0) return;

  calculate_slaves_container_size(workspace->output, slave_count,
                                  &container_width, &container_height);

  i = 0;
  wl_list_for_each(toplevel, &workspace->slaves, link) {
    width = container_width;
    height = container_height;
    toplevel_strip_decorations_of_size(toplevel, &width, &height);

    x = output->usable_area.x + output->usable_area.width * server.config->master_ratio + inner_gaps;
    y = output->usable_area.y + outer_gaps
      + container_height * i
      + inner_gaps * 2 * i;
    toplevel_adjust_buffer_start_position(toplevel, &x, &y);

    toplevel_set_pending_state(toplevel, x, y, width, height);
    i++;
  }
}

/* this function assumes they are in the same workspace and
 * that t2 comes after t1 if in the same list */
void
layout_swap_tiled_toplevels(struct mwc_toplevel *t1, struct mwc_toplevel *t2) {
  struct wl_list *before_t1 = t1->link.prev;
  wl_list_remove(&t1->link);
  wl_list_insert(&t2->link, &t1->link);
  wl_list_remove(&t2->link);
  wl_list_insert(before_t1, &t2->link);

  layout_set_pending_state(t1->workspace);
}

struct mwc_toplevel *
layout_find_closest_tiled_toplevel(struct mwc_workspace *workspace, bool master,
                                   enum mwc_direction side) {
  /* this means there are no tiled toplevels */
  if(wl_list_empty(&workspace->masters)) return NULL;

  struct mwc_toplevel *first_master = wl_container_of(workspace->masters.next,
                                                      first_master, link);
  struct mwc_toplevel *last_master = wl_container_of(workspace->masters.prev,
                                                     last_master, link);

  struct mwc_toplevel *first_slave = NULL;
  struct mwc_toplevel *last_slave = NULL;
  if(!wl_list_empty(&workspace->slaves)) {
    first_slave = wl_container_of(workspace->slaves.next, first_slave, link);
    last_slave = wl_container_of(workspace->slaves.prev, last_slave, link);
  }

  switch(side) {
    case MWC_UP: {
      if(master || first_slave == NULL) return first_master;
      return first_slave;
    }
    case MWC_DOWN: {
      if(master || last_slave == NULL) return first_master;
      return last_slave;
    }
    case MWC_LEFT: {
      return first_master;
    }
    case MWC_RIGHT: {
      if(last_slave != NULL) return last_slave;
      return last_master;
    }
  }
}

struct mwc_toplevel *
layout_toplevel_at(struct mwc_workspace *workspace, uint32_t x, uint32_t y) {
  struct mwc_toplevel *t;
  wl_list_for_each(t, &workspace->masters, link) {
    uint32_t decorations_left = t->link.prev == &workspace->masters
      ? server.config->outer_gaps + server.config->border_width
      : server.config->inner_gaps + server.config->border_width;

    uint32_t decorations_right = wl_list_empty(&workspace->slaves) 
      ? server.config->outer_gaps + server.config->border_width
      : server.config->inner_gaps + server.config->border_width;

    uint32_t decorations_top = server.config->outer_gaps + server.config->border_width;

    uint32_t decorations_bottom = server.config->outer_gaps + server.config->border_width;

    struct wlr_box box = {
      .x = t->current.x - decorations_left,
      .y = t->current.y - decorations_top,
      .width = t->current.width + decorations_left + decorations_right + 1,
      .height = t->current.height + decorations_top + decorations_bottom + 1,
    };

    if(wlr_box_contains_point(&box, x, y)) {
      return t;
    }
  }

  wl_list_for_each(t, &workspace->slaves, link) {
    uint32_t decorations_left = server.config->inner_gaps + server.config->border_width;
    uint32_t decorations_right = server.config->outer_gaps + server.config->border_width;

    uint32_t decorations_top = t->link.prev == &workspace->slaves
      ? server.config->outer_gaps + server.config->border_width
      : server.config->inner_gaps + server.config->border_width;

    uint32_t decorations_bottom = t->link.next == &workspace->slaves
      ? server.config->outer_gaps + server.config->border_width
      : server.config->inner_gaps + server.config->border_width;

    struct wlr_box box = {
      .x = t->current.x - decorations_left,
      .y = t->current.y - decorations_top,
      .width = t->current.width + decorations_left + decorations_right + 1,
      .height = t->current.height + decorations_top + decorations_bottom + 1,
    };

    if(wlr_box_contains_point(&box, x, y)) {
      return t;
    }
  }

  return NULL;
}

