#pragma once

#include <stdint.h>
#include <unistd.h>
#include <wlr/util/box.h>

struct vec2 {
  double x, y;
};

void
run_cmd(char *cmd);

int
box_area(struct wlr_box *box);

