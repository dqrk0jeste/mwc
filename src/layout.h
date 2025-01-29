#pragma once

#include "output.h"
#include "notwc.h"

#include <stdint.h>

void
calculate_masters_dimensions(struct notwc_output *output, uint32_t master_count,
                             uint32_t slave_count, uint32_t *width, uint32_t *height);

void
calculate_slaves_dimensions(struct notwc_output *output, uint32_t slave_count,
                            uint32_t *width, uint32_t *height);

bool
toplevel_is_master(struct notwc_toplevel *toplevel);

bool
toplevel_is_slave(struct notwc_toplevel *toplevel);

void
layout_set_pending_state(struct notwc_workspace *workspace);

/* this function assumes they are in the same workspace and
 * that t2 comes after t1 if in the same list */
void
layout_swap_tiled_toplevels(struct notwc_toplevel *t1,
                            struct notwc_toplevel *t2);

struct notwc_toplevel *
layout_find_closest_tiled_toplevel(struct notwc_workspace *workspace, bool master,
                                   enum notwc_direction side);

struct notwc_toplevel *
layout_find_closest_floating_toplevel(struct notwc_workspace *workspace,
                                      enum notwc_direction side);
