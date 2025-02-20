#include <scenefx/types/wlr_scene.h>

#include "keybinds.h"

#include "config.h"
#include "helpers.h"
#include "mwc.h"
#include "toplevel.h"
#include "workspace.h"
#include "layout.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wayland-util.h>
#include <wlr/backend/session.h>
#include <wlr/xcursor.h>
#include <wlr/types/wlr_cursor.h>

extern struct mwc_server server;

bool
server_handle_keybinds(struct mwc_keyboard *keyboard, uint32_t keycode,
                       enum wl_keyboard_key_state state) {
  if(server.lock != NULL) return false;

  uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
  /* we use empty state so we can get raw, unmodified key.
   * this is used becuase we already handle modifiers explicitly,
   * and dont want them to interfere. for example, shift would make it
   * harder to specify the right key e.g. we would have to write
   *   keybind alt+shift # <do_something>
   * instead of
   *   alt+shift 3 <do_something> */

  const xkb_keysym_t *syms;
  int count = xkb_state_key_get_syms(keyboard->empty, keycode, &syms);

  bool handled = handle_change_vt_key(syms, count);
  if(handled) return true;

  struct keybind *k;
  for(size_t i = 0; i < count; i++) {
    wl_list_for_each(k, &server.config->keybinds, link) {
      if(!k->initialized) continue;

      if(k->active && k->stop && syms[i] == k->key
         && state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        k->active = false;
        k->stop(k->args);
        return true;
      }

      if(modifiers == k->modifiers && syms[i] == k->key
         && state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        k->active = true;
        k->action(k->args);
        return true;
      }
    }
  }

  return false;
}

bool
handle_change_vt_key(const xkb_keysym_t *keysyms, size_t count) {
	for(int i = 0; i < count; i++) {
	  uint32_t vt = keysyms[i] - XKB_KEY_XF86Switch_VT_1 + 1;
		if (vt >= 1 && vt <= 12) {
      wlr_session_change_vt(server.session, vt);
			return true;
		}
	}
	return false;
}

void
keybind_stop_server(void *data) {
  server.running = false;
  wl_display_terminate(server.wl_display);
}

void
keybind_run(void *data) {
  run_cmd(data);
}

void
keybind_change_workspace(void *data) {
  struct mwc_workspace *workspace = data;
  change_workspace(workspace, server.grabbed_toplevel != NULL);
}

void
keybind_next_workspace(void *data) {
  struct mwc_workspace *current = server.active_workspace;
  struct wl_list *next = current->link.next;
  if(next == &current->output->workspaces) {
    next = current->output->workspaces.next;
  }
  struct mwc_workspace *next_workspace = wl_container_of(next, next_workspace, link);
  change_workspace(next_workspace, server.grabbed_toplevel != NULL);
}

void
keybind_prev_workspace(void *data) {
  struct mwc_workspace *current = server.active_workspace;
  struct wl_list *prev = current->link.prev;
  if(prev == &current->output->workspaces) {
    prev = current->output->workspaces.prev;
  }
  struct mwc_workspace *prev_workspace = wl_container_of(prev, prev_workspace, link);
  change_workspace(prev_workspace, server.grabbed_toplevel != NULL);
}

void
keybind_move_focused_toplevel_to_workspace(void *data) {
  struct mwc_toplevel *toplevel = server.focused_toplevel;
  if(toplevel == NULL || toplevel == server.grabbed_toplevel) return;

  struct mwc_workspace *workspace = data;
  toplevel_move_to_workspace(toplevel, workspace);
}

void
keybind_resize_focused_toplevel(void *data) {
  struct mwc_toplevel *toplevel = get_pointer_focused_toplevel();
  if(toplevel == NULL || !toplevel->floating || toplevel->animation.running) return;

  uint32_t edges = toplevel_get_closest_corner(server.cursor, toplevel);

  char cursor_image[128] = {0};
  if(edges & WLR_EDGE_TOP) {
    strcat(cursor_image, "top_");
  } else {
    strcat(cursor_image, "bottom_");
  }
  if(edges & WLR_EDGE_LEFT) {
    strcat(cursor_image, "left_");
  } else {
    strcat(cursor_image, "right_");
  }
  strcat(cursor_image, "corner");

  wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, cursor_image);

  server.client_driven_move_resize = false;
  toplevel_start_resize(toplevel, edges);
}

void
keybind_stop_resize_focused_toplevel(void *data) {
  if(server.grabbed_toplevel == NULL) return;

  struct mwc_output *primary_output = 
    toplevel_get_primary_output(server.grabbed_toplevel);
  if(primary_output != server.grabbed_toplevel->workspace->output) {
    server.grabbed_toplevel->workspace = primary_output->active_workspace;
    wl_list_remove(&server.grabbed_toplevel->link);
    wl_list_insert(&primary_output->active_workspace->floating_toplevels,
                   &server.grabbed_toplevel->link);
  }

  server_reset_cursor_mode();
}

void
keybind_move_focused_toplevel(void *data) {
  struct mwc_toplevel *toplevel = get_pointer_focused_toplevel();
  if(toplevel == NULL || toplevel->fullscreen || toplevel->animation.running) return;

  wlr_cursor_set_xcursor(server.cursor, server.cursor_mgr, "hand1");

  server.client_driven_move_resize = false;
  toplevel_start_move(toplevel);
}

void
keybind_stop_move_focused_toplevel(void *data) {
  if(server.grabbed_toplevel == NULL) return;

  if(!server.grabbed_toplevel->floating) {
    toplevel_tiled_insert_into_layout(server.grabbed_toplevel,
                                      server.cursor->x, server.cursor->y);
  } else {
    struct mwc_output *primary_output = toplevel_get_primary_output(server.grabbed_toplevel);
    server.grabbed_toplevel->workspace = primary_output->active_workspace;
    wl_list_insert(&primary_output->active_workspace->floating_toplevels,
                   &server.grabbed_toplevel->link);
  }

  server_reset_cursor_mode();
  layout_set_pending_state(server.active_workspace);
}

void
keybind_close_keyboard_focused_toplevel(void *data) {
  struct mwc_toplevel *toplevel = server.focused_toplevel;
  if(toplevel == NULL) return;

  xdg_toplevel_send_close(toplevel->xdg_toplevel->resource);
}

void
keybind_move_focus(void *data) {
  uint64_t direction = (uint64_t)data;

  struct mwc_toplevel *toplevel = server.focused_toplevel;
  /* we need grabbed toplevel toplevel to keep focus */
  if(toplevel == server.grabbed_toplevel) return;

  enum mwc_direction opposite_side;
  switch(direction) {
    case MWC_UP:
      opposite_side = MWC_DOWN;
      break;
    case MWC_DOWN:
      opposite_side = MWC_UP;
      break;
    case MWC_LEFT:
      opposite_side = MWC_RIGHT;
      break;
    case MWC_RIGHT:
      opposite_side = MWC_LEFT;
      break;
  }

  /* if no toplevel has keyboard focus then get the output
   * the pointer is on and try from there */
  if(toplevel == NULL) {
    struct wlr_output *wlr_output = wlr_output_layout_output_at(
      server.output_layout, server.cursor->x, server.cursor->y);
    struct mwc_output *output = wlr_output->data;
    struct mwc_output *relative_output = output_get_relative(output, direction);
    if(relative_output != NULL) {
      focus_output(relative_output, opposite_side);
    }
    return;
  }

  /* get the toplevels output */
  struct mwc_workspace *workspace = toplevel->workspace;
  struct mwc_output *output = toplevel->workspace->output;
  struct mwc_output *relative_output =
    output_get_relative(toplevel->workspace->output, direction);

  if(toplevel->fullscreen) {
    struct mwc_output *relative_output = output_get_relative(output, direction);
    if(relative_output != NULL) {
      focus_output(relative_output, opposite_side);
    }
    return;
  }

  if(toplevel->floating) {
    struct mwc_toplevel *closest = toplevel_find_closest_floating_on_workspace(toplevel, direction);
    if(closest != NULL) {
      focus_toplevel(closest);
      cursor_jump_focused_toplevel();
      return;
    }
    struct mwc_output *relative_output = output_get_relative(output, direction);
    if(relative_output != NULL) {
      focus_output(relative_output, opposite_side);
    }
    return;
  }

  struct wl_list *next;
  if(toplevel_is_master(toplevel)) {
    switch(direction) {
      case MWC_RIGHT: {
        next = toplevel->link.next;
        if(next == &workspace->masters) {
          next = workspace->slaves.prev;
          if(next == &workspace->slaves) {
            if(relative_output != NULL) {
              focus_output(relative_output, opposite_side);
            }
            return;
          }
        }
        struct mwc_toplevel *t = wl_container_of(next, t, link);
        focus_toplevel(t);
        cursor_jump_focused_toplevel();
        return;
      }
      case MWC_LEFT: {
        next = toplevel->link.prev;
        if(next == &workspace->masters) {
          if(relative_output != NULL) {
            focus_output(relative_output, opposite_side);
          }
          return;
        }
        struct mwc_toplevel *t = wl_container_of(next, t, link);
        focus_toplevel(t);
        cursor_jump_focused_toplevel();
        return;
      }
      default: {
        if(relative_output != NULL) {
          focus_output(relative_output, opposite_side);
        }
        return;
      }
    }
  }

  /* only case left is that the toplevel is a slave */
  switch(direction) {
    case MWC_LEFT: {
      struct mwc_toplevel *last_master =
        wl_container_of(workspace->masters.prev, last_master, link);
      focus_toplevel(last_master);
      cursor_jump_focused_toplevel();
      return;
    }
    case MWC_RIGHT: {
      if(relative_output != NULL) {
        focus_output(relative_output, opposite_side);
      }
      return;
    }
    case MWC_UP: {
      struct wl_list *above = toplevel->link.prev;
      if(above == &workspace->slaves) {
        if(relative_output != NULL) {
          focus_output(relative_output, opposite_side);
        }
        return;
      }
      struct mwc_toplevel *t = wl_container_of(above, t, link);
      focus_toplevel(t);
      cursor_jump_focused_toplevel();
      return;
    }
    case MWC_DOWN: {
      struct wl_list *bellow = toplevel->link.next;
      if(bellow == &workspace->slaves) {
        if(relative_output != NULL) {
          focus_output(relative_output, opposite_side);
        }
        return;
      }
      struct mwc_toplevel *t = wl_container_of(bellow, t, link);
      focus_toplevel(t);
      cursor_jump_focused_toplevel();
      return;
    }
  }
}


void
keybind_swap_focused_toplevel(void *data) {
  uint64_t direction = (uint64_t)data;

  struct mwc_toplevel *toplevel = server.focused_toplevel;
  if(toplevel == NULL || toplevel == server.grabbed_toplevel) return;

  struct mwc_workspace *workspace = toplevel->workspace;
  struct mwc_output *relative_output =
    output_get_relative(workspace->output, direction);

  if(toplevel->floating || toplevel->fullscreen) {
    if(relative_output != NULL
      && relative_output->active_workspace->fullscreen_toplevel == NULL) {
      toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
    }
    return;
  }

  struct wl_list *next;
  if(toplevel_is_master(toplevel)) {
    switch(direction) {
      case MWC_RIGHT: {
        next = toplevel->link.next;
        if(next == &workspace->masters) {
          next = workspace->slaves.prev;
          if(next == &workspace->slaves) {
            if(relative_output != NULL
               && relative_output->active_workspace->fullscreen_toplevel == NULL) {
              toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
            }
            return;
          }
        }
        struct mwc_toplevel *t = wl_container_of(next, t, link);
        layout_swap_tiled_toplevels(toplevel, t);
        return;
      }
      case MWC_LEFT: {
        next = toplevel->link.prev;
        if(next == &workspace->masters) {
          if(relative_output != NULL
             && relative_output->active_workspace->fullscreen_toplevel == NULL) {
            toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
          }
          return;
        }
        struct mwc_toplevel *t = wl_container_of(next, t, link);
        layout_swap_tiled_toplevels(t, toplevel);
        return;
      }
      default: {
        struct mwc_output *relative_output =
          output_get_relative(workspace->output, direction);
        if(relative_output != NULL
           && relative_output->active_workspace->fullscreen_toplevel == NULL) {
          toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
        }
        return;
      }
    }
  }

  switch(direction) {
    case MWC_LEFT: {
      struct mwc_toplevel *last_master =
        wl_container_of(workspace->masters.prev, last_master, link);
      layout_swap_tiled_toplevels(toplevel, last_master);
      return;
    }
    case MWC_RIGHT: {
      struct mwc_output *relative_output =
        output_get_relative(workspace->output, direction);
      if(relative_output != NULL
         && relative_output->active_workspace->fullscreen_toplevel == NULL) {
        toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
      }
      return;
    }
    case MWC_UP: {
      next = toplevel->link.prev;
      if(next == &workspace->slaves) {
        if(relative_output != NULL
           && relative_output->active_workspace->fullscreen_toplevel == NULL) {
          toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
        }
        return;
      }
      struct mwc_toplevel *t = wl_container_of(next, t, link);
      layout_swap_tiled_toplevels(t, toplevel);
      return;
    }
    case MWC_DOWN: {
      next = toplevel->link.next;
      if(next == &workspace->slaves) {
        if(relative_output != NULL
           && relative_output->active_workspace->fullscreen_toplevel == NULL) {
          toplevel_move_to_workspace(toplevel, relative_output->active_workspace);
        }
        return;
      }
      struct mwc_toplevel *t = wl_container_of(next, t, link);
      layout_swap_tiled_toplevels(toplevel, t);
      return;
    }
  }
}

void
keybind_focused_toplevel_toggle_floating(void *data) {
  struct mwc_toplevel *toplevel = server.focused_toplevel;
  if(toplevel == NULL || toplevel->fullscreen || toplevel == server.grabbed_toplevel) return;

  if(toplevel->floating) {
    toplevel->floating = false;
    wl_list_remove(&toplevel->link);

    if(wl_list_length(&toplevel->workspace->masters) < server.config->master_count) {
      wl_list_insert(toplevel->workspace->masters.prev, &toplevel->link);
    } else {
      wl_list_insert(toplevel->workspace->slaves.prev, &toplevel->link);
    }

    wlr_scene_node_reparent(&toplevel->scene_tree->node, server.tiled_tree);
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);

    layout_set_pending_state(toplevel->workspace);
    return;
  }

  toplevel->floating = true;
  if(toplevel_is_master(toplevel)) {
    if(!wl_list_empty(&toplevel->workspace->slaves)) {
      struct mwc_toplevel *s = wl_container_of(toplevel->workspace->slaves.prev, s, link);
      wl_list_remove(&s->link);
      wl_list_insert(toplevel->workspace->masters.prev, &s->link);
    }
    wl_list_remove(&toplevel->link);
  } else {
    wl_list_remove(&toplevel->link);
  }

  wl_list_insert(&toplevel->workspace->floating_toplevels, &toplevel->link);

  struct wlr_box output_box = toplevel->workspace->output->usable_area;
  uint32_t width, height;
  toplevel_floating_size(toplevel, &width, &height);
  toplevel_set_pending_state(toplevel, UINT32_MAX, UINT32_MAX, width, height);

  wlr_scene_node_reparent(&toplevel->scene_tree->node, server.floating_tree);
  wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);

  layout_set_pending_state(toplevel->workspace);
}

void
keybind_focused_toplevel_toggle_fullscreen(void* data) {
  struct mwc_toplevel *toplevel = server.focused_toplevel;
  if(toplevel == NULL || toplevel == server.grabbed_toplevel) return;

  if(toplevel->fullscreen) {
    toplevel_unset_fullscreen(toplevel);
  } else {
    toplevel_set_fullscreen(toplevel);
  }
}
