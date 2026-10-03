#include "core/surface_mgr.h"
#include "core/wayland.h"
#include "core/output.h"
#include "util/log.h"
#include "zwlr-layer-shell-v1-client-protocol.h"
#include <stdlib.h>
#include <string.h>

#define BAR_HEIGHT 32

static void layer_surface_configure(void *data,
                                   struct zwlr_layer_surface_v1 *ls,
                                   uint32_t serial,
                                   uint32_t width,
                                   uint32_t height)
{
    (void)width;
    (void)height;
    struct layer_surface *lsurf = (struct layer_surface *)data;
    if (lsurf && ls) {
        zwlr_layer_surface_v1_ack_configure(ls, serial);
        lsurf->configured_w = width;
        lsurf->configured_h = height;
    }
}

static void layer_surface_closed(void *data,
                                 struct zwlr_layer_surface_v1 *ls)
{
    (void)ls;
    struct layer_surface *lsurf = (struct layer_surface *)data;
    if (lsurf) {
        lsurf->ls = NULL;
        lsurf->surf = NULL;
    }
    log_info("layer surface closed");
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

void surface_mgr_init(struct surface_mgr *mgr, struct wayland_ctx *ctx, struct wl_list *outputs)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->ctx = ctx;
    mgr->outputs = outputs;
    wl_list_init(&mgr->layers);
    mgr->shutdown = false;
}

void surface_mgr_fini(struct surface_mgr *mgr)
{
    struct layer_surface *lsurf, *tmp;
    wl_list_for_each_safe(lsurf, tmp, &mgr->layers, link) {
        if (lsurf->ls) {
            zwlr_layer_surface_v1_destroy(lsurf->ls);
            lsurf->ls = NULL;
        }
        if (lsurf->surf) {
            wl_surface_destroy(lsurf->surf);
            lsurf->surf = NULL;
        }
        wl_list_remove(&lsurf->link);
        free(lsurf);
    }
}

static struct layer_surface *find_layer_for_output(struct surface_mgr *mgr, struct output *out)
{
    struct layer_surface *lsurf;
    wl_list_for_each(lsurf, &mgr->layers, link) {
        if (lsurf->out == out) {
            return lsurf;
        }
    }
    return NULL;
}

void surface_mgr_on_output_added(struct surface_mgr *mgr, struct output *out)
{
    if (!mgr || !out) {
        return;
    }
    if (!mgr->ctx->layer_shell || !mgr->ctx->compositor) {
        log_warn("layer_shell/compositor not ready; cannot create layer surface");
        return;
    }
    struct layer_surface *lsurf = find_layer_for_output(mgr, out);
    if (lsurf) {
        log_debug("layer surface already exists for output");
        return;
    }
    lsurf = calloc(1, sizeof(*lsurf));
    if (!lsurf) {
        log_err("calloc layer_surface failed");
        return;
    }
    lsurf->out = out;
    lsurf->anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    lsurf->exclusive_zone = BAR_HEIGHT;
    lsurf->configured_w = 0;
    lsurf->configured_h = 0;

    lsurf->surf = wl_compositor_create_surface(mgr->ctx->compositor);
    if (!lsurf->surf) {
        log_err("failed to create wl_surface");
        free(lsurf);
        return;
    }
    lsurf->ls = zwlr_layer_shell_v1_get_layer_surface(mgr->ctx->layer_shell,
                                                      lsurf->surf,
                                                      out->wl,
                                                      ZWLR_LAYER_SHELL_V1_LAYER_TOP,
                                                      "emancipation-shell");
    if (!lsurf->ls) {
        log_err("failed to create zwlr_layer_surface");
        wl_surface_destroy(lsurf->surf);
        free(lsurf);
        return;
    }
    zwlr_layer_surface_v1_set_anchor(lsurf->ls, lsurf->anchor);
    uint32_t w = (out->w > 0) ? (uint32_t)out->w : 0;
    zwlr_layer_surface_v1_set_size(lsurf->ls, w, BAR_HEIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(lsurf->ls, lsurf->exclusive_zone);
    zwlr_layer_surface_v1_add_listener(lsurf->ls, &layer_surface_listener, lsurf);
    wl_surface_commit(lsurf->surf);
    wl_list_insert(&mgr->layers, &lsurf->link);
    log_info("layer surface created for output (w=%d)", out->w);
}

void surface_mgr_on_output_removed(struct surface_mgr *mgr, struct output *out)
{
    if (!mgr || !out) {
        return;
    }
    struct layer_surface *lsurf = find_layer_for_output(mgr, out);
    if (!lsurf) {
        return;
    }
    if (lsurf->ls) {
        zwlr_layer_surface_v1_destroy(lsurf->ls);
        lsurf->ls = NULL;
    }
    if (lsurf->surf) {
        wl_surface_destroy(lsurf->surf);
        lsurf->surf = NULL;
    }
    wl_list_remove(&lsurf->link);
    free(lsurf);
    log_info("layer surface removed for output");
}

void surface_mgr_relayout(struct surface_mgr *mgr)
{
    if (!mgr) {
        return;
    }
    struct layer_surface *lsurf;
    wl_list_for_each(lsurf, &mgr->layers, link) {
        if (!lsurf->ls || !lsurf->out) {
            continue;
        }
        uint32_t w = (lsurf->out->w > 0) ? (uint32_t)lsurf->out->w : 0;
        zwlr_layer_surface_v1_set_size(lsurf->ls, w, BAR_HEIGHT);
        zwlr_layer_surface_v1_set_exclusive_zone(lsurf->ls, lsurf->exclusive_zone);
        wl_surface_commit(lsurf->surf);
    }
}
