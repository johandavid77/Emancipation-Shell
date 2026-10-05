#define _GNU_SOURCE
#include "core/surface_mgr.h"
#include "core/wayland.h"
#include "core/output.h"
#include "util/log.h"
#include "zwlr-layer-shell-v1-client-protocol.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

#define BAR_HEIGHT 32
#define BAR_BG 0xFF1F1F1F /* ARGB: gris oscuro */

struct shm_buffer {
    struct wl_buffer *buf;
    void *data;
    size_t size;
    int fd;
    int width;
    int height;
    int stride;
};
static void shm_buffer_destroy(struct shm_buffer *sb)
{
    if (!sb) return;
    if (sb->buf) wl_buffer_destroy(sb->buf);
    if (sb->data && sb->data != MAP_FAILED) munmap(sb->data, sb->size);
    if (sb->fd >= 0) close(sb->fd);
    memset(sb, 0, sizeof(*sb));
    sb->fd = -1;
}

static int shm_buffer_create(struct wl_shm *shm, struct shm_buffer *sb, int w, int h)
{
    if (!shm || !sb || w <= 0 || h <= 0) return -1;
    memset(sb, 0, sizeof(*sb));
    sb->fd = -1;
    sb->width = w;
    sb->height = h;
    sb->stride = w * 4;
    sb->size = sb->stride * h;
    char name[] = "/emancipation-shm-XXXXXX";
    sb->fd = mkstemp(name);
    if (sb->fd < 0) return -1;
    unlink(name);
    if (ftruncate(sb->fd, sb->size) < 0) {
        close(sb->fd);
        sb->fd = -1;
        return -1;
    }
    sb->data = mmap(NULL, sb->size, PROT_READ | PROT_WRITE, MAP_SHARED, sb->fd, 0);
    if (sb->data == MAP_FAILED) {
        sb->data = NULL;
        close(sb->fd);
        sb->fd = -1;
        return -1;
    }
    uint32_t *p = (uint32_t *)sb->data;
    for (int i = 0; i < w * h; i++) {
        p[i] = BAR_BG;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, sb->fd, sb->size);
    if (!pool) {
        munmap(sb->data, sb->size);
        sb->data = NULL;
        close(sb->fd);
        sb->fd = -1;
        return -1;
    }
    sb->buf = wl_shm_pool_create_buffer(pool, 0, w, h, sb->stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    return sb->buf ? 0 : -1;
}

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
        if (!lsurf->surf) return;
        int w = (width > 0) ? (int)width : ((lsurf->out && lsurf->out->w > 0) ? lsurf->out->w : 0);
        int h = (height > 0) ? (int)height : BAR_HEIGHT;
        if (w < 0) w = 0;
        if (h < 0) h = 0;
        lsurf->configured_w = w;
        lsurf->configured_h = h;
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
    lsurf->ctx = mgr->ctx;
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
                                                      ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
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
                wl_surface_commit(lsurf->surf);
    }
}
