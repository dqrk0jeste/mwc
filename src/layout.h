#pragma once

#include "output.h"
#include "mwc.h"

#include <stdint.h>

void
calculate_masters_dimensions(struct mwc_output *output, uint32_t master_count,
                             uint32_t slave_count, uint32_t *width, uint32_t *height);

void
calculate_slaves_dimensions(struct mwc_output *output, uint32_t slave_count,
                            uint32_t *width, uint32_t *height);

bool
toplevel_is_master(struct mwc_toplevel *toplevel);

bool
toplevel_is_slave(struct mwc_toplevel *toplevel);

void
layout_set_pending_state(struct mwc_workspace *workspace);

/* this function assumes they are in the same workspace and
 * that t2 comes after t1 if in the same list */
void
layout_swap_tiled_toplevels(struct mwc_toplevel *t1,
                            struct mwc_toplevel *t2);

struct mwc_toplevel *
layout_find_closest_tiled_toplevel(struct mwc_workspace *workspace, bool master,
                                   enum mwc_direction side);

struct mwc_toplevel *
layout_find_closest_floating_toplevel(struct mwc_workspace *workspace,
                                      enum mwc_direction side);

struct mwc_toplevel *
layout_toplevel_at(struct mwc_workspace *workspace, uint32_t x, uint32_t y);

