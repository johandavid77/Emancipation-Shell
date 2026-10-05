#ifndef EMANCIPATION_SURFACE_MGR_H
#define EMANCIPATION_SURFACE_MGR_H

#include <wayland-client.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct wayland_ctx;
struct output;
struct config;
struct workspace_manager;

struct shm_buf {
    struct wl_buffer *wl;
    void *data;
    size_t size;
    int width;  /* pixels */
    int height; /* pixels */
    bool busy;  /* held by the compositor until wl_buffer.release */
};

struct layer_surface {
    struct surface_mgr *mgr;
    struct zwlr_layer_surface_v1 *ls;
    struct wl_surface *surf;
    struct output *out;
    struct wl_callback *frame_cb;
    struct shm_buf bufs[2];
    int width;  /* logical, from configure */
    int height; /* logical, from configure */
    bool configured;
    bool dirty;
    struct wl_list link;
};

struct surface_mgr {
    struct wayland_ctx *ctx;
    struct wl_list layers;   /* layer_surface.link */
    struct wl_list *outputs; /* owned by main */
    const struct config *cfg;
    struct workspace_manager *wm;
    int bar_height;
    bool shutdown;
};

void surface_mgr_init(struct surface_mgr *mgr, struct wayland_ctx *ctx, struct wl_list *outputs);
void surface_mgr_fini(struct surface_mgr *mgr);
void surface_mgr_set_sources(struct surface_mgr *mgr, const struct config *cfg, struct workspace_manager *wm);
void surface_mgr_apply_config(struct surface_mgr *mgr, const struct config *cfg);
void surface_mgr_request_redraw(struct surface_mgr *mgr);
void surface_mgr_on_output_added(struct surface_mgr *mgr, struct output *out);
void surface_mgr_on_output_removed(struct surface_mgr *mgr, struct output *out);

#endif /* EMANCIPATION_SURFACE_MGR_H */
