#ifndef EMANCIPATION_REGISTRY_H
#define EMANCIPATION_REGISTRY_H

#include <wayland-client.h>
#include "core/wayland.h"

struct registry_state {
    struct wayland_ctx *ctx;
    struct wl_list *outputs;
    struct surface_mgr *mgr;
};

void registry_init(struct registry_state *state, struct wayland_ctx *ctx, struct wl_list *outputs, struct surface_mgr *mgr);
void registry_bind_globals(struct registry_state *state);

#endif /* EMANCIPATION_REGISTRY_H */
