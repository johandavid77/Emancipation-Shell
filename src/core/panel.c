#define _GNU_SOURCE
#include "core/panel.h"
#include "core/output.h"
#include "core/wayland.h"
#include "util/log.h"
#include "zwlr-layer-shell-v1-client-protocol.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

/* Panel draw happens straight into the shm buffer, exactly like the bar does,
 * so the buffer lifecycle (double buffer + frame callback) is mirrored here. */

static void panel_buf_release(void *data, struct wl_buffer *wl)
{
    (void)wl;
    struct shm_buf *b = data;
    b->busy = false;
}

static const struct wl_buffer_listener panel_buf_listener = {
    .release = panel_buf_release,
};

static void panel_buf_destroy(struct shm_buf *b)
{
    if (b->wl) wl_buffer_destroy(b->wl);
    if (b->data) munmap(b->data, b->size);
    memset(b, 0, sizeof(*b));
}

static bool panel_buf_create(struct wl_shm *shm, struct shm_buf *b, int w, int h)
{
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
    size_t size = (size_t)stride * (size_t)h;
    int fd = memfd_create("emancipation-panel", MFD_CLOEXEC);
    if (fd < 0) return false;
    if (ftruncate(fd, (off_t)size) < 0) { close(fd); return false; }
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return false; }
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    b->wl = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    b->data = data;
    b->size = size;
    b->width = w;
    b->height = h;
    b->busy = false;
    wl_buffer_add_listener(b->wl, &panel_buf_listener, b);
    return true;
}

static struct shm_buf *panel_free_buf(struct panel *p, int w, int h)
{
    for (int i = 0; i < 2; i++) {
        struct shm_buf *b = &p->bufs[i];
        if (b->busy) continue;
        if (b->wl && (b->width != w || b->height != h)) panel_buf_destroy(b);
        if (!b->wl && !panel_buf_create(p->ctx->shm, b, w, h)) return NULL;
        return b;
    }
    return NULL;
}

static void panel_frame_done(void *data, struct wl_callback *cb, uint32_t t)
{
    (void)t;
    struct panel *p = data;
    wl_callback_destroy(cb);
    p->frame_cb = NULL;
    if (p->dirty) panel_present(p);
}

static const struct wl_callback_listener panel_frame_listener = {
    .done = panel_frame_done,
};

static void panel_draw(struct panel *p)
{
    if (!p->configured || !p->surf || !p->visible) return;
    if (p->frame_cb) return;
    int scale = (p->out && p->out->scale > 0) ? p->out->scale : 1;
    int pw = p->logical_w * scale;
    int ph = p->logical_h * scale;
    if (pw <= 0 || ph <= 0) return;

    struct shm_buf *b = panel_free_buf(p, pw, ph);
    if (!b) return;

    cairo_surface_t *cs = cairo_image_surface_create_for_data(
        b->data, CAIRO_FORMAT_ARGB32, pw, ph,
        cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, pw));
    cairo_t *cr = cairo_create(cs);
    cairo_scale(cr, scale, scale);
    if (p->draw) p->draw(p, cr, p->logical_w, p->logical_h);
    cairo_destroy(cr);
    cairo_surface_flush(cs);
    cairo_surface_destroy(cs);

    wl_surface_set_buffer_scale(p->surf, scale);
    wl_surface_attach(p->surf, b->wl, 0, 0);
    wl_surface_damage_buffer(p->surf, 0, 0, pw, ph);
    p->frame_cb = wl_surface_frame(p->surf);
    wl_callback_add_listener(p->frame_cb, &panel_frame_listener, p);
    wl_surface_commit(p->surf);
    b->busy = true;
    p->dirty = false;
}

void panel_present(struct panel *p)
{
    panel_draw(p);
}

static void panel_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial,
                            uint32_t w, uint32_t h)
{
    struct panel *p = data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    if (w > 0) p->logical_w = (int)w;
    if (h > 0) p->logical_h = (int)h;
    p->configured = true;
    p->dirty = true;
    panel_draw(p);
}

static void panel_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
    (void)ls;
    struct panel *p = data;
    log_info("panel closed by compositor");
    p->visible = false;
    p->configured = false;
}

static const struct zwlr_layer_surface_v1_listener panel_ls_listener = {
    .configure = panel_configure,
    .closed = panel_closed,
};

void panel_init(struct panel *p, struct wayland_ctx *ctx)
{
    memset(p, 0, sizeof(*p));
    p->ctx = ctx;
}

static void panel_teardown_surface(struct panel *p)
{
    if (p->frame_cb) wl_callback_destroy(p->frame_cb);
    p->frame_cb = NULL;
    if (p->ls) { zwlr_layer_surface_v1_destroy(p->ls); p->ls = NULL; }
    if (p->surf) { wl_surface_destroy(p->surf); p->surf = NULL; }
    panel_buf_destroy(&p->bufs[0]);
    panel_buf_destroy(&p->bufs[1]);
    p->configured = false;
    p->visible = false;
    wl_list_remove(&p->link);
    p->link.prev = p->link.next = NULL;
}

void panel_fini(struct panel *p)
{
    if (!p) return;
    panel_teardown_surface(p);
}

void panel_set_callbacks(struct panel *p, panel_draw_fn draw, panel_key_fn key, panel_click_fn click,
                         panel_motion_fn motion, void *userdata)
{
    p->draw = draw;
    p->key = key;
    p->click = click;
    p->motion = motion;
    p->userdata = userdata;
}

bool panel_show(struct panel *p, struct output *out, int w, int h)
{
    if (!p || !p->ctx || !p->ctx->layer_shell || !p->ctx->compositor || !p->ctx->shm) return false;
    if (p->visible) return true;
    if (!out) return false;

    p->out = out;
    p->logical_w = w;
    p->logical_h = h;
    p->w = w;
    p->h = h;
    p->surf = wl_compositor_create_surface(p->ctx->compositor);
    if (!p->surf) return false;
    p->ls = zwlr_layer_shell_v1_get_layer_surface(p->ctx->layer_shell, p->surf, out->wl,
                                                   ZWLR_LAYER_SHELL_V1_LAYER_TOP, "emancipation-panel");
    zwlr_layer_surface_v1_add_listener(p->ls, &panel_ls_listener, p);
    zwlr_layer_surface_v1_set_anchor(p->ls, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_exclusive_zone(p->ls, 0); /* overlay: reserve nothing */
    zwlr_layer_surface_v1_set_keyboard_interactivity(p->ls, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    zwlr_layer_surface_v1_set_size(p->ls, (uint32_t)w, (uint32_t)h);
    /* center on the output */
    int px = out->w > w ? (out->w - w) / 2 : 0;
    int py = out->h > h ? (out->h - h) / 2 : 0;
    {
        int opx = out->x + px;
        int opy = out->y + py;
        zwlr_layer_surface_v1_set_margin(p->ls, opy, 0, 0, opx);
    }
    p->visible = true;
    p->dirty = true;
    wl_surface_commit(p->surf); /* initial commit without buffer -> configure */
    log_info("panel shown: %dx%d at %d,%d on output %u", w, h, px, py, out->global_name);
    return true;
}

void panel_hide(struct panel *p)
{
    if (!p || !p->visible) return;
    log_info("panel hidden");
    panel_teardown_surface(p);
}

bool panel_is_visible(const struct panel *p)
{
    return p && p->visible;
}

void panel_mark_dirty(struct panel *p)
{
    if (!p || !p->visible) return;
    p->dirty = true;
    panel_draw(p);
}