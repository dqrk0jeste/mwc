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
calculate_masters_dimensions(struct mwc_output *output, uint32_t master_count,
                             uint32_t slave_count, uint32_t *width, uint32_t *height) {
  uint32_t outer_gaps = server.config->outer_gaps;
  uint32_t inner_gaps = server.config->inner_gaps;
  double master_ratio = server.config->master_ratio;
  double border_width = server.config->border_width;

  struct wlr_box output_box = output->usable_area;

  uint32_t total_width = slave_count > 0
    ? output_box.width * master_ratio
    : output_box.width;

  uint32_t total_decorations = slave_count > 0
    ? outer_gaps // left outer gaps
      + master_count * 2 * border_width // all borders
      + (master_count - 1) * 2 * inner_gaps // inner gaps between masters
      + inner_gaps // right inner gaps 
    : outer_gaps // left outer gaps
      + master_count * 2 * border_width // all borders
      + (master_count - 1) * 2 * inner_gaps // inner gaps between masters
      + outer_gaps; // right outer gaps

  *width = (total_width - total_decorations) / master_count;
  *height = output_box.height - 2 * outer_gaps - 2 * border_width;
}

void
calculate_slaves_dimensions(struct mwc_output *output, uint32_t slave_count,
                            uint32_t *width, uint32_t *height) {
  uint32_t outer_gaps = server.config->outer_gaps;
  uint32_t inner_gaps = server.config->inner_gaps;
  double master_ratio = server.config->master_ratio;
  double border_width = server.config->border_width;

  struct wlr_box output_box = output->usable_area;

  *width = output_box.width * (1 - master_ratio)
    - outer_gaps - inner_gaps
    - 2 * border_width;
  *height = (output_box.height - 2 * outer_gaps
    - (slave_count - 1) * 2 * inner_gaps
    - slave_count * 2 * border_width) / slave_count;
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
  double master_ratio = server.config->master_ratio;
  double border_width = server.config->border_width;

  uint32_t slave_count = wl_list_length(&workspace->slaves);
  uint32_t master_count = wl_list_length(&workspace->masters);

  uint32_t master_width, master_height;
  calculate_masters_dimensions(output, master_count, slave_count,
                               &master_width, &master_height);

  struct mwc_toplevel *m;
  size_t i = 0;
  wl_list_for_each(m, &workspace->masters, link) {
    uint32_t master_x = output->usable_area.x + outer_gaps
      + (master_width + 2 * border_width) * i
      + 2 * inner_gaps * i
      + border_width;
    uint32_t master_y = output->usable_area.y + outer_gaps
      + border_width;

    if(m->mapped) {
      toplevel_set_pending_state(m, master_x, master_y, master_width, master_height);
    } else {
      toplevel_set_initial_state(m, master_x, master_y, master_width, master_height);
    }
    i++;
  }

  if(slave_count == 0) return;

  /* share the remaining space among slaves */
  uint32_t slave_width, slave_height, slave_x, slave_y;
  calculate_slaves_dimensions(workspace->output, slave_count, &slave_width, &slave_height);

  struct mwc_toplevel *s;
  i = 0;
  wl_list_for_each(s, &workspace->slaves, link) {
    slave_x = output->usable_area.x + output->usable_area.width * master_ratio
      + inner_gaps + border_width;
    slave_y = output->usable_area.y + outer_gaps
      + i * (slave_height + inner_gaps * 2 + 2 * border_width)
      + border_width;

    if(s->mapped) {
      toplevel_set_pending_state(s, slave_x, slave_y, slave_width, slave_height);
    } else {
      toplevel_set_initial_state(s, slave_x, slave_y, slave_width, slave_height);
    }
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
layout_find_closest_floating_toplevel(struct mwc_workspace *workspace,
                                      enum mwc_direction side) {
  struct wl_list *l = workspace->floating_toplevels.next;
  if(l == &workspace->floating_toplevels) return NULL;

  struct mwc_toplevel *t = wl_container_of(l, t, link);

  struct mwc_toplevel *min_x = t;
  struct mwc_toplevel *max_x = t;
  struct mwc_toplevel *min_y = t;
  struct mwc_toplevel *max_y = t;

  wl_list_for_each(t, &workspace->floating_toplevels, link) {
    if(X(t) < X(min_x)) {
      min_x = t;
    } else if(X(t) > X(max_x)) {
      max_x = t;
    }
    if(Y(t) < Y(min_y)) {
      min_y = t;
    } else if(Y(t) > Y(max_y)) {
      max_y = t;
    }
  }

  switch(side) {
    case MWC_UP: return min_y;
    case MWC_DOWN: return max_y;
    case MWC_LEFT: return min_x;
    case MWC_RIGHT: return max_x;
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

    uint32_t decorations_top = t->link.prev == &workspace->masters
      ? server.config->outer_gaps + server.config->border_width
      : server.config->inner_gaps + server.config->border_width;

    uint32_t decorations_bottom = t->link.next == &workspace->masters
      ? server.config->outer_gaps + server.config->border_width
      : server.config->inner_gaps + server.config->border_width;

    struct wlr_box box = {
      .x = t->current.x - decorations_left,
      .y = t->current.y - decorations_top,
      .width = t->current.width + decorations_left + decorations_right,
      .height = t->current.height + decorations_top + decorations_bottom,
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
      .width = t->current.width + decorations_left + decorations_right,
      .height = t->current.height + decorations_top + decorations_bottom,
    };

    if(wlr_box_contains_point(&box, x, y)) {
      return t;
    }
  }

  return NULL;
}

