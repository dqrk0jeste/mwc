#include "something.h"

#include "owl.h"
#include "layer_surface.h"
#include "session_lock.h"

#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>

extern struct owl_server server;

struct owl_something *
root_parent_of_surface(struct wlr_surface *wlr_surface) {
  struct wlr_surface *root_wlr_surface =
    wlr_surface_get_root_surface(wlr_surface);

  struct wlr_scene_tree *tree;
  struct wlr_xdg_surface *xdg_surface =
    wlr_xdg_surface_try_from_wlr_surface(root_wlr_surface);

  if(xdg_surface != NULL) {
    tree = xdg_surface->data;
  } else {
    struct wlr_layer_surface_v1 *wlr_layer_surface =
      wlr_layer_surface_v1_try_from_wlr_surface(root_wlr_surface);
    if(wlr_layer_surface != NULL) {
      struct owl_layer_surface *layer_surface = wlr_layer_surface->data;
      tree = layer_surface->scene->tree;
    } else {
      struct wlr_session_lock_surface_v1 *wlr_lock_surface =
        wlr_session_lock_surface_v1_try_from_wlr_surface(root_wlr_surface);
      if(wlr_lock_surface != NULL) {
        struct owl_lock_surface *lock_surface = wlr_lock_surface->data;
        tree = lock_surface->scene_tree;
      } else {
        return NULL;
      }
    }
  }

  struct owl_something *something = tree->node.data;
  while(something == NULL || something->type == OWL_POPUP) {
    tree = tree->node.parent;
    something = tree->node.data;
  }

  return something;
}

struct owl_something *
something_at(double lx, double ly, struct wlr_surface **surface,
             double *sx, double *sy) {
  /* this returns the topmost node in the scene at the given layout coords */
  struct wlr_scene_node *node =
    wlr_scene_node_at(&server.scene->tree.node, lx, ly, sx, sy);
  if(node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
    return NULL;
  }

  struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
  struct wlr_scene_surface *scene_surface =
    wlr_scene_surface_try_from_buffer(scene_buffer);
  if(!scene_surface) {
    return NULL;
  }

  *surface = scene_surface->surface;

  struct wlr_scene_tree *tree = node->parent;
  struct owl_something *something = tree->node.data;
  while(something == NULL || something->type == OWL_POPUP) {
    tree = tree->node.parent;
    something = tree->node.data;
  }

  return something;
}

