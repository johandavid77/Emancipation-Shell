#include "core/registry.h"
#include "core/output.h"
#include "core/seat.h"
#include "core/surface_mgr.h"
#include "bar/workspaces.h"
#include "util/log.h"
#include "zwlr-layer-shell-v1-client-protocol.h"
#include "ext-workspace-v1-client-protocol.h"
#include <stdlib.h>
#include <string.h>

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface, uint32_t version)
{
    struct registry_state *state = (struct registry_state *)data;
    if (strcmp(interface, "wl_compositor") == 0) {
        state->ctx->compositor = (struct wl_compositor *)wl_registry_bind(registry, name,
                                                                           &wl_compositor_interface,
                                                                           version >= 4 ? 4 : version);
        log_debug("bound wl_compositor");
    } else if (strcmp(interface, "wl_shm") == 0) {
        state->ctx->shm = (struct wl_shm *)wl_registry_bind(registry, name,
                                                            &wl_shm_interface,
                                                            version >= 1 ? 1 : version);
        log_debug("bound wl_shm");
    } else if (strcmp(interface, "wl_seat") == 0) {
        struct wl_seat *wl_s = wl_registry_bind(registry, name, &wl_seat_interface,
                                                 version < 5 ? version : 5);
        if (state->ctx->seat) {
            seat_attach(state->ctx->seat, wl_s);
            log_info("wl_seat bound");
        } else {
            wl_seat_destroy(wl_s);
        }
        return;
    } else if (strcmp(interface, "wp_cursor_shape_manager_v1") == 0) {
        state->ctx->cursor_shape_mgr = (struct wp_cursor_shape_manager_v1 *)
            wl_registry_bind(registry, name, &wp_cursor_shape_manager_v1_interface, 1);
        log_info("cursor_shape_manager bound");
        return;
    } else if (strcmp(interface, "zwlr_layer_shell_v1") == 0) {
        state->ctx->layer_shell = (struct zwlr_layer_shell_v1 *)wl_registry_bind(registry, name,
                                                                                  &zwlr_layer_shell_v1_interface,
                                                                                  version < 4 ? version : 4);
        log_debug("bound zwlr_layer_shell_v1");
    } else if (strcmp(interface, "ext_workspace_manager_v1") == 0) {
        uint32_t v = version < (uint32_t)ext_workspace_manager_v1_interface.version
                         ? version : (uint32_t)ext_workspace_manager_v1_interface.version;
        state->ctx->workspace_manager = (struct ext_workspace_manager_v1 *)wl_registry_bind(registry, name,
                                                                                           &ext_workspace_manager_v1_interface,
                                                                                           v);
        workspaces_bind(state->ctx->workspaces);
        log_debug("bound ext_workspace_manager_v1");
    } else if (strcmp(interface, "wl_output") == 0) {
        struct wl_output *wl_out = (struct wl_output *)wl_registry_bind(registry, name,
                                                                        &wl_output_interface,
                                                                        version >= 3 ? 3 : version);
        struct output *out = calloc(1, sizeof(*out));
        if (out) {
            output_init(out, name, wl_out);
            wl_list_insert(state->outputs, &out->link);
            if (state->mgr) {
                surface_mgr_on_output_added(state->mgr, out);
            }
            log_info("output added (global %u)", name);
        } else {
            wl_output_destroy(wl_out);
            log_err("calloc output failed");
        }
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name)
{
    (void)registry;
    struct registry_state *state = (struct registry_state *)data;
    struct output *out, *tmp;
    wl_list_for_each_safe(out, tmp, state->outputs, link) {
        if (out->global_name == name) {
            if (state->mgr) {
                surface_mgr_on_output_removed(state->mgr, out);
            }
            wl_list_remove(&out->link);
            output_destroy(out);
            free(out);
            log_info("output removed (global %u)", name);
            return;
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

void registry_init(struct registry_state *state, struct wayland_ctx *ctx, struct wl_list *outputs, struct surface_mgr *mgr)
{
    state->ctx = ctx;
    state->outputs = outputs;
    state->mgr = mgr;
}

void registry_bind_globals(struct registry_state *state)
{
    state->ctx->registry = wl_display_get_registry(state->ctx->display);
    wl_registry_add_listener(state->ctx->registry, &registry_listener, state);
}
