#ifndef EMANCIPATION_WAYLAND_H
#define EMANCIPATION_WAYLAND_H

#include <wayland-client.h>
#include "cursor-shape-v1-client-protocol.h"

struct zwlr_layer_shell_v1;
struct ext_workspace_manager_v1;
struct workspace_manager;
struct wp_cursor_shape_manager_v1;
struct seat_state;

struct wayland_ctx {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct ext_workspace_manager_v1 *workspace_manager;
    struct workspace_manager *workspaces; /* listener attached on bind */
    struct wp_cursor_shape_manager_v1 *cursor_shape_mgr;
    struct seat_state *seat; /* single seat, created by seat_create() */
};

#endif /* EMANCIPATION_WAYLAND_H */
