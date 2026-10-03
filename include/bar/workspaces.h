#ifndef EMANCIPATION_WORKSPACES_H
#define EMANCIPATION_WORKSPACES_H

#include <wayland-client.h>
#include <stdbool.h>

struct workspace_manager;
struct wayland_ctx;

struct workspace_manager *workspaces_create(struct wayland_ctx *ctx);
void workspaces_destroy(struct workspace_manager *wm);
void workspaces_bind(struct workspace_manager *wm);

#endif /* EMANCIPATION_WORKSPACES_H */
