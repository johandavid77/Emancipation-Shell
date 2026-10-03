#ifndef EMANCIPATION_WAYLAND_H
#define EMANCIPATION_WAYLAND_H

#include <wayland-client.h>

struct zwlr_layer_shell_v1;
struct ext_workspace_manager_v1;

struct wayland_ctx {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct ext_workspace_manager_v1 *workspace_manager;
};

#endif /* EMANCIPATION_WAYLAND_H */
