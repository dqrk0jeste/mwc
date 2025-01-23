#include "session_lock.h"

#include "layer_surface.h"
#include "something.h"
#include "toplevel.h"
#include "owl.h"
#include "rendering.h"
#include "wlr/util/log.h"

#include <wayland-server-core.h>
#include <wayland-util.h>

extern struct owl_server server;

void
session_lock_handle_new_surface(struct wl_listener *listener, void *data) {
	struct owl_lock *lock = wl_container_of(listener, lock, new_surface);

	struct wlr_session_lock_surface_v1 *wlr_lock_surface = data;
  struct owl_output *output = wlr_lock_surface->output->data;

  struct owl_lock_surface *lock_surface = calloc(1, sizeof(*lock_surface));
  lock_surface->wlr_lock_surface = wlr_lock_surface;

	lock_surface->scene_tree = wlr_scene_subsurface_tree_create(server.session_lock_tree,
                                                              wlr_lock_surface->surface);
  wlr_lock_surface->data = lock_surface;

  lock_surface->something.type = OWL_LOCK_SURFACE;
  lock_surface->something.lock_surface = lock_surface;

  lock_surface->scene_tree->node.data = &lock_surface->something;

  struct wlr_box output_box;
  wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

  wlr_scene_node_set_position(&lock_surface->scene_tree->node, output_box.x, output_box.y);
  wlr_session_lock_surface_v1_configure(wlr_lock_surface, output_box.width, output_box.height);

  focus_lock_surface(lock_surface);
}

void
focus_lock_surface(struct owl_lock_surface *lock_surface) {
  struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server.seat);
  if(keyboard != NULL) {
    wlr_seat_keyboard_notify_enter(server.seat, lock_surface->wlr_lock_surface->surface,
                                   keyboard->keycodes, keyboard->num_keycodes,
                                   &keyboard->modifiers);
  }
}

void
session_lock_handle_unlock(struct wl_listener *listener, void *data) {
  struct owl_lock *lock = wl_container_of(listener, lock, unlock);
  lock->locked = false;
  server.lock = NULL;

  struct wlr_output *wlr_output = wlr_output_layout_output_at(
    server.output_layout, server.cursor->x, server.cursor->y);
  struct owl_output *output = wlr_output->data;

  bool focused = false;
  struct owl_layer_surface *l;
  wl_list_for_each(l, &output->layers.overlay, link) {
    if(l->wlr_layer_surface->current.keyboard_interactive) {
      focus_layer_surface(l);
      focused = true;
    }
  }
  wl_list_for_each(l, &output->layers.top, link) {
    if(l->wlr_layer_surface->current.keyboard_interactive) {
      focus_layer_surface(l);
      focused = true;
    }
  }

  if(!focused) {
    if(!wl_list_empty(&server.active_workspace->masters)) {
      struct owl_toplevel *first = wl_container_of(server.active_workspace->masters.next,
                                                   first, link);
      focus_toplevel(first);
    } else if(!wl_list_empty(&server.active_workspace->floating_toplevels)) {
      struct owl_toplevel *first = wl_container_of(server.active_workspace->floating_toplevels.next,
                                                   first, link);
      focus_toplevel(first);
    }
  }

  struct owl_output *o; 
  wl_list_for_each(o, &server.outputs, link) {
    /* destroy the rectangle blocking the view */
    wlr_scene_node_destroy(&o->session_lock_rect->node);
    o->session_lock_rect = NULL;
  }
}

void
session_lock_handle_destroy(struct wl_listener *listener, void *data) {
	struct owl_lock *lock = wl_container_of(listener, lock, destroy);

  if(lock->locked) {
    wlr_log(WLR_ERROR, "lock surface destroyed without being unlocked");
  }

	wl_list_remove(&lock->destroy.link);
	wl_list_remove(&lock->unlock.link);
	wl_list_remove(&lock->new_surface.link);

  free(lock);
}

void
session_lock_manager_handle_destroy(struct wl_listener *listener, void *data) {
	wl_list_remove(&server.lock_manager_destroy.link);
	wl_list_remove(&server.new_lock.link);
}

void
session_lock_manager_handle_new(struct wl_listener *listener, void *data) {
  struct wlr_session_lock_v1 *wlr_lock = data;
  
  if(server.lock != NULL) {
    wlr_log(WLR_ERROR, "session already locked");
    wlr_session_lock_v1_destroy(wlr_lock);
    return;
  }

  struct owl_lock *lock = calloc(1, sizeof(*lock));
  lock->wlr_lock = wlr_lock;
  lock->locked = true;
  server.lock = lock;

  float black[4] = { 0.0, 0.0, 0.0, 1.0 };
  struct owl_output *o;
  wl_list_for_each(o, &server.outputs, link) {
    struct wlr_box output_box;
    wlr_output_layout_get_box(server.output_layout, o->wlr_output, &output_box);

    o->session_lock_rect = wlr_scene_rect_create(server.session_lock_tree,
                                                 output_box.width, output_box.height, black);
    wlr_scene_node_set_position(&o->session_lock_rect->node, output_box.x, output_box.y);
  }

  unfocus_focused_toplevel();

  lock->new_surface.notify = session_lock_handle_new_surface;
  wl_signal_add(&wlr_lock->events.new_surface, &lock->new_surface);

  lock->unlock.notify = session_lock_handle_unlock;
  wl_signal_add(&wlr_lock->events.unlock, &lock->unlock);

  lock->destroy.notify = session_lock_handle_destroy;
  wl_signal_add(&wlr_lock->events.destroy, &lock->destroy);

  wlr_session_lock_v1_send_locked(wlr_lock);
}
