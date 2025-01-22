#include "session_lock.h"

#include "layer_surface.h"
#include "owl.h"
#include "rendering.h"
#include "wlr/util/log.h"

#include <wayland-server-core.h>

extern struct owl_server server;

void
session_lock_handle_new_surface(struct wl_listener *listener, void *data) {
  wlr_log(WLR_ERROR, "new surface");
	struct owl_lock *lock = wl_container_of(listener, lock, new_surface);

	struct wlr_session_lock_surface_v1 *lock_surface = data;
  struct owl_output *output = lock_surface->output->data;

	struct wlr_scene_tree *surface_tree =
		wlr_scene_subsurface_tree_create(server.session_lock_tree, lock_surface->surface);
  wlr_scene_node_raise_to_top(&surface_tree->node);

  struct wlr_box output_box;
  wlr_output_layout_get_box(server.output_layout, output->wlr_output, &output_box);

  wlr_scene_node_set_position(&surface_tree->node, output_box.x, output_box.y);
  wlr_session_lock_surface_v1_configure(lock_surface, output_box.width, output_box.height);
}

void
session_lock_handle_unlock(struct wl_listener *listener, void *data) {
  struct owl_lock *lock = wl_container_of(listener, lock, destroy);
  lock->locked = false;

  if(server.focused_layer_surface != NULL) {
    focus_layer_surface(server.focused_layer_surface);
  } else if(server.focused_toplevel) {
    /* this wont work */
    /*focus_toplevel(server.focused_layer_surface);*/
  }

  /* destroy the rectangle blocking the view */
  /*wlr_scene_node_destroy(&lock->rect->node);*/
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

  float black[4] = { 0.0, 0.0, 0.0, 0.0 };
  lock->rect = wlr_scene_rect_create(server.session_lock_tree, 5000, 5000, black);

  lock->new_surface.notify = session_lock_handle_new_surface;
  wl_signal_add(&wlr_lock->events.new_surface, &lock->new_surface);

  lock->unlock.notify = session_lock_handle_unlock;
  wl_signal_add(&wlr_lock->events.unlock, &lock->unlock);

  lock->destroy.notify = session_lock_handle_destroy;
  wl_signal_add(&wlr_lock->events.destroy, &lock->destroy);

  wlr_session_lock_v1_send_locked(wlr_lock);
}
