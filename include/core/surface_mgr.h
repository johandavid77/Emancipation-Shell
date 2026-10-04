#ifndef EMANCIPATION_SURFACE_MGR_H
#define EMANCIPATION_SURFACE_MGR_H

#include <wayland-client.h>
#include <stdint.h>
#include <stdbool.h>

struct wayland_ctx;
struct output;

struct layer_surface {
    struct zwlr_layer_surface_v1 *ls;
    struct wl_surface *surf;
    struct output *out;
    uint32_t configured_w;
    uint32_t configured_h;
    uint32_t anchor;
    int32_t exclusive_zone;
    struct wayland_ctx *ctx;
    struct wl_buffer *bar_buf;
    void *bar_data;
    size_t bar_size;
    int bar_fd;
    int bar_stride;
    struct wl_list link;
};

struct surface_mgr {
    struct wayland_ctx *ctx;
    struct wl_list layers; /* list of layer_surface */
    struct wl_list *outputs; /* reference to outputs list */
    bool shutdown;
};

void surface_mgr_init(struct surface_mgr *mgr, struct wayland_ctx *ctx, struct wl_list *outputs);
void surface_mgr_fini(struct surface_mgr *mgr);
void surface_mgr_on_output_added(struct surface_mgr *mgr, struct output *out);
void surface_mgr_on_output_removed(struct surface_mgr *mgr, struct output *out);
void surface_mgr_relayout(struct surface_mgr *mgr);

#endif /* EMANCIPATION_SURFACE_MGR_H */
