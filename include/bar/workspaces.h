#ifndef EMANCIPATION_WORKSPACES_H
#define EMANCIPATION_WORKSPACES_H

#include <wayland-client.h>
#include <stdbool.h>
#include <stddef.h>

struct workspace_manager;
struct wayland_ctx;

struct workspace_info {
    char name[64];
    bool active;
    bool urgent;
    bool hidden;
};

typedef void (*workspaces_changed_cb)(void *userdata);

struct workspace_manager *workspaces_create(struct wayland_ctx *ctx);
void workspaces_destroy(struct workspace_manager *wm);
/* Attach to ctx->workspace_manager. Call right after binding the global so
 * the initial burst of events is not lost. */
void workspaces_bind(struct workspace_manager *wm);
void workspaces_set_changed_cb(struct workspace_manager *wm, workspaces_changed_cb cb, void *userdata);
/* Fill `out` with workspaces shown on `output` (NULL = all), ordered by
 * coordinates, then name. Returns the number written. */
size_t workspaces_snapshot(struct workspace_manager *wm, struct wl_output *output,
                           struct workspace_info *out, size_t max);

#endif /* EMANCIPATION_WORKSPACES_H */
