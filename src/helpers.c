#include "helpers.h"

void
run_cmd(char *cmd) {
  if(fork() == 0) {
    execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
  }
}

int
box_area(struct wlr_box *box) {
  return box->width * box->height;
}

