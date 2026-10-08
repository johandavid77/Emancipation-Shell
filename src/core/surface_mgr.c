#define _GNU_SOURCE
#include "core/surface_mgr.h"
#include "core/wayland.h"
#include "core/panel.h"
#include "compositors/niri_ipc.h"
#include "core/output.h"
#include "bar/bar.h"
#include "config/config.h"
#include "bar/workspaces.h"
#include "launcher/launcher.h"
#include "util/log.h"
#include "control/control_center.h"
#include "zwlr-layer-shell-v1-client-protocol.h"

#include <cairo.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#define DEFAULT_BAR_HEIGHT 32
#define NAMESPACE "emancipation-shell"

/* ---- shm buffers --------------------------------------------------------- */

static void buf_release(void *data, struct wl_buffer *wl)
{
    (void)wl;
    struct shm_buf *b = data;
    b->busy = false;
}

static const struct wl_buffer_listener buf_listener = {
    .release = buf_release,
};

static void buf_destroy(struct shm_buf *b)
{
    if (b->wl) wl_buffer_destroy(b->wl);
    if (b->data) munmap(b->data, b->size);
    memset(b, 0, sizeof(*b));
}

static bool buf_create(struct wl_shm *shm, struct shm_buf *b, int w, int h)
{
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
    size_t size = (size_t)stride * (size_t)h;
    int fd = memfd_create("emancipation-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) {
        log_err("memfd_create failed");
        return false;
    }
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return false;
    }
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return false;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    b->wl = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    b->data = data;
    b->size = size;
    b->width = w;
    b->height = h;
    b->busy = false;
    wl_buffer_add_listener(b->wl, &buf_listener, b);
    return true;
}

static struct shm_buf *get_free_buf(struct layer_surface *l, int w, int h)
{
    for (int i = 0; i < 2; i++) {
        struct shm_buf *b = &l->bufs[i];
        if (b->busy) continue;
        if (b->wl && (b->width != w || b->height != h)) buf_destroy(b);
        if (!b->wl && !buf_create(l->mgr->ctx->shm, b, w, h)) return NULL;
        return b;
    }
    return NULL; /* both in flight: draw again on release/frame */
}

/* ---- drawing ------------------------------------------------------------- */

static void layer_draw(struct layer_surface *l);

static void frame_done(void *data, struct wl_callback *cb, uint32_t t)
{
    (void)t;
    struct layer_surface *l = data;
    wl_callback_destroy(cb);
    l->frame_cb = NULL;
    if (l->dirty) layer_draw(l);
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done,
};

static void layer_draw(struct layer_surface *l)
{
    if (!l->configured || !l->surf || l->frame_cb) return;
    int scale = (l->out && l->out->scale > 0) ? l->out->scale : 1;
    int pw = l->width * scale;
    int ph = l->height * scale;
    if (pw <= 0 || ph <= 0) return;

    struct shm_buf *b = get_free_buf(l, pw, ph);
    if (!b) return; /* keep dirty, retried on next frame */

    cairo_surface_t *cs = cairo_image_surface_create_for_data(
        b->data, CAIRO_FORMAT_ARGB32, pw, ph,
        cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, pw));
    cairo_t *cr = cairo_create(cs);
    cairo_scale(cr, scale, scale);
    struct bar_ctx bctx = {
        .cfg = l->mgr->cfg,
        .wm = l->mgr->wm,
        .sys = l->mgr->sys,
        .niri = l->mgr->niri,
        .launcher = l->mgr->launcher,
        .output = l->out ? l->out->wl : NULL,
        .hover_x = l->hover_x,
    };
    bar_draw(cr, l->width, l->height, &bctx, &l->hits);
    cairo_destroy(cr);
    cairo_surface_flush(cs);
    cairo_surface_destroy(cs);

    wl_surface_set_buffer_scale(l->surf, scale);
    wl_surface_attach(l->surf, b->wl, 0, 0);
    wl_surface_damage_buffer(l->surf, 0, 0, pw, ph);
    l->frame_cb = wl_surface_frame(l->surf);
    wl_callback_add_listener(l->frame_cb, &frame_listener, l);
    wl_surface_commit(l->surf);
    b->busy = true;
    l->dirty = false;
}

/* ---- layer surface ------------------------------------------------------- */

static void layer_destroy(struct layer_surface *l)
{
    if (l->frame_cb) wl_callback_destroy(l->frame_cb);
    if (l->ls) zwlr_layer_surface_v1_destroy(l->ls);
    if (l->surf) wl_surface_destroy(l->surf);
    buf_destroy(&l->bufs[0]);
    buf_destroy(&l->bufs[1]);
    wl_list_remove(&l->link);
    free(l);
}

static void ls_configure(void *data, struct zwlr_layer_surface_v1 *ls,
                         uint32_t serial, uint32_t w, uint32_t h)
{
    struct layer_surface *l = data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    l->width = w > 0 ? (int)w : (l->out && l->out->w > 0 ? l->out->w : 1);
    l->height = h > 0 ? (int)h : l->mgr->bar_height;
    l->configured = true;
    l->dirty = true;
    layer_draw(l);
}

static void ls_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
    (void)ls;
    struct layer_surface *l = data;
    log_info("layer surface closed by compositor");
    layer_destroy(l);
}

static const struct zwlr_layer_surface_v1_listener ls_listener = {
    .configure = ls_configure,
    .closed = ls_closed,
};

static struct layer_surface *find_layer(struct surface_mgr *mgr, struct output *out)
{
    struct layer_surface *l;
    wl_list_for_each(l, &mgr->layers, link) {
        if (l->out == out) return l;
    }
    return NULL;
}

static void layer_apply_geometry(struct surface_mgr *mgr, struct layer_surface *l)
{
    zwlr_layer_surface_v1_set_size(l->ls, 0, (uint32_t)mgr->bar_height);
    zwlr_layer_surface_v1_set_exclusive_zone(l->ls, mgr->bar_height);
}

/* ---- public -------------------------------------------------------------- */

void surface_mgr_init(struct surface_mgr *mgr, struct wayland_ctx *ctx, struct wl_list *outputs)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->ctx = ctx;
    mgr->outputs = outputs;
    mgr->bar_height = DEFAULT_BAR_HEIGHT;
    wl_list_init(&mgr->layers);
}

void surface_mgr_fini(struct surface_mgr *mgr)
{
    struct layer_surface *l, *tmp;
    wl_list_for_each_safe(l, tmp, &mgr->layers, link) {
        layer_destroy(l);
    }
}

void surface_mgr_set_sources(struct surface_mgr *mgr, const struct config *cfg, struct workspace_manager *wm)
{
    mgr->cfg = cfg;
    mgr->wm = wm;
    if (cfg && cfg->bar.height > 0) mgr->bar_height = cfg->bar.height;
}

void surface_mgr_on_output_added(struct surface_mgr *mgr, struct output *out)
{
    if (!mgr || !out) return;
    /* globals may be announced before layer_shell/shm; main retries after
     * the initial roundtrip */
    if (!mgr->ctx->layer_shell || !mgr->ctx->compositor || !mgr->ctx->shm) return;
    if (mgr->cfg && !mgr->cfg->bar.visible) return;
    if (find_layer(mgr, out)) return;

    struct layer_surface *l = calloc(1, sizeof(*l));
    if (!l) return;
    l->mgr = mgr;
    l->out = out;
    l->hover_x = -1;
    l->surf = wl_compositor_create_surface(mgr->ctx->compositor);
    l->ls = zwlr_layer_shell_v1_get_layer_surface(mgr->ctx->layer_shell, l->surf, out->wl,
                                                  ZWLR_LAYER_SHELL_V1_LAYER_TOP, NAMESPACE);
    zwlr_layer_surface_v1_add_listener(l->ls, &ls_listener, l);
    zwlr_layer_surface_v1_set_anchor(l->ls, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    layer_apply_geometry(mgr, l);
    /* the bar never takes keyboard focus */
    zwlr_layer_surface_v1_set_keyboard_interactivity(l->ls, 0);
    wl_surface_commit(l->surf); /* initial commit without buffer -> configure */
    wl_list_insert(&mgr->layers, &l->link);
    log_info("bar surface created (output global %u, height %d)", out->global_name, mgr->bar_height);
}

void surface_mgr_on_output_removed(struct surface_mgr *mgr, struct output *out)
{
    if (!mgr || !out) return;
    struct layer_surface *l = find_layer(mgr, out);
    if (l) {
        layer_destroy(l);
        log_info("bar surface removed (output global %u)", out->global_name);
    }
}

void surface_mgr_apply_config(struct surface_mgr *mgr, const struct config *cfg)
{
    if (!mgr || !cfg) return;
    mgr->cfg = cfg;
    if (!cfg->bar.visible) {
        surface_mgr_fini(mgr);
        return;
    }
    int new_h = cfg->bar.height > 0 ? cfg->bar.height : DEFAULT_BAR_HEIGHT;
    bool resized = new_h != mgr->bar_height;
    mgr->bar_height = new_h;

    struct output *out;
    wl_list_for_each(out, mgr->outputs, link) {
        surface_mgr_on_output_added(mgr, out); /* no-op if it exists */
    }
    struct layer_surface *l;
    wl_list_for_each(l, &mgr->layers, link) {
        if (resized) {
            layer_apply_geometry(mgr, l);
            wl_surface_commit(l->surf); /* compositor answers with configure */
        }
        l->dirty = true;
        layer_draw(l);
    }
}

void surface_mgr_request_redraw(struct surface_mgr *mgr)
{
    if (!mgr) return;
    struct layer_surface *l;
    wl_list_for_each(l, &mgr->layers, link) {
        l->dirty = true;
        layer_draw(l);
    }
}

void surface_mgr_set_sysinfo(struct surface_mgr *mgr, const struct sysinfo_state *sys)
{
    if (mgr) mgr->sys = sys;
}

static struct panel *find_panel(struct surface_mgr *mgr, struct wl_surface *surf)
{
    for (int i = 0; i < mgr->n_panels; i++) {
        if (mgr->panels[i] && mgr->panels[i]->surf == surf && panel_is_visible(mgr->panels[i]))
            return mgr->panels[i];
    }
    return NULL;
}

static struct layer_surface *find_by_surface(struct surface_mgr *mgr, struct wl_surface *surf)
{
    struct layer_surface *l;
    wl_list_for_each(l, &mgr->layers, link) {
        if (l->surf == surf) return l;
    }
    return NULL;
}

bool surface_mgr_pointer_motion(struct surface_mgr *mgr, struct wl_surface *surf, double x, double y)
{
    struct panel *pn = find_panel(mgr, surf);
    if (pn) {
        pn->hover_x = x;
        pn->hover_y = y;
        if (pn->motion) pn->motion(pn, x, y);
        return true;
    }
    struct layer_surface *l = find_by_surface(mgr, surf);
    if (!l) return false;
    const struct bar_hit *before = bar_hit_at(&l->hits, l->hover_x);
    const struct bar_hit *now = bar_hit_at(&l->hits, x);
    l->hover_x = x;
    if (before != now) { /* only repaint when the hovered item changes */
        l->dirty = true;
        layer_draw(l);
    }
    return now != NULL;
}

void surface_mgr_pointer_leave(struct surface_mgr *mgr, struct wl_surface *surf)
{
    if (find_panel(mgr, surf)) return;
    struct layer_surface *l = find_by_surface(mgr, surf);
    if (!l) return;
    bool had = bar_hit_at(&l->hits, l->hover_x) != NULL;
    l->hover_x = -1;
    if (had) {
        l->dirty = true;
        layer_draw(l);
    }
}

void surface_mgr_pointer_click(struct surface_mgr *mgr, struct wl_surface *surf, double x, double y)
{
    struct panel *pn = find_panel(mgr, surf);
    if (pn) {
        if (pn->click) pn->click(pn, x, y);
        return;
    }
    /* Click outside any panel: close all panels */
    if (mgr->n_panels > 0) {
        for (int i = 0; i < mgr->n_panels; i++) {
            if (mgr->panels[i] && panel_is_visible(mgr->panels[i])) {
                surface_mgr_hide_all_panels(mgr);
                return;
            }
        }
    }
    struct layer_surface *l = find_by_surface(mgr, surf);
    if (!l) return;
    const struct bar_hit *hit = bar_hit_at(&l->hits, x);
    if (hit) {
        if (hit->kind == BAR_HIT_WORKSPACE) workspaces_activate(mgr->wm, hit->ref);
        if (hit->kind == BAR_HIT_LAUNCHER) {
            if (mgr->on_launcher) mgr->on_launcher(mgr->on_launcher_userdata);
            log_info("launcher toggled");
        }
        if ((hit->kind == BAR_HIT_CLOCK || hit->kind == BAR_HIT_DATE) && mgr->on_calendar) {
            mgr->on_calendar(mgr->on_calendar_userdata);
            log_info("calendar toggled");
        }
        if (hit->kind == BAR_HIT_TASKBAR && mgr->niri) {
            const struct niri_window *w = niri_ipc_window(mgr->niri, (int)(intptr_t)hit->ref);
            if (w) {
                if (w->focused) niri_ipc_close(mgr->niri, w->id);
                else niri_ipc_focus(mgr->niri, w->id);
            }
            log_info("taskbar clicked");
        }
        if (hit->kind == BAR_HIT_KBD) { 
            pid_t pid = fork(); if (pid==0){ setsid(); execl("/bin/sh","sh","-c","echo 'KBD'",NULL); _exit(0);} 
        }
    }
}

void surface_mgr_pointer_scroll(struct surface_mgr *mgr, struct wl_surface *surf, int dir)
{
    if (find_panel(mgr, surf)) return;
    struct layer_surface *l = find_by_surface(mgr, surf);
    if (!l) return;
    workspaces_step(mgr->wm, l->out ? l->out->wl : NULL, dir);
}

void surface_mgr_mark_dirty_all(struct surface_mgr *mgr)
{
    struct layer_surface *l;
    if (!mgr) return;
    wl_list_for_each(l, &mgr->layers, link) {
        l->dirty = true;
        layer_draw(l);
    }
}
void surface_mgr_set_niri(struct surface_mgr *mgr, struct niri_ipc *n)
{
    if (mgr) mgr->niri = n;
}

void surface_mgr_set_launcher(struct surface_mgr *mgr, struct launcher *launcher)
{
    if (mgr) mgr->launcher = launcher;
}

void surface_mgr_add_panel(struct surface_mgr *mgr, struct panel *p)
{
    if (!mgr || !p || mgr->n_panels >= SURFACE_MGR_MAX_PANELS) return;
    mgr->panels[mgr->n_panels++] = p;
}

bool surface_mgr_handle_key(struct surface_mgr *mgr, uint32_t keysym, uint32_t mods)
{
    for (int i = 0; i < mgr->n_panels; i++) {
        struct panel *p = mgr->panels[i];
        if (!p || !panel_is_visible(p)) continue;
        if (p->key) return p->key(p, keysym, mods);
        return false;
    }
    return false;
}

struct output *surface_mgr_first_output(struct surface_mgr *mgr)
{
    if (!mgr || !mgr->outputs || wl_list_empty(mgr->outputs)) return NULL;
    return wl_container_of(mgr->outputs->next, (struct output *)mgr->outputs->next, link);
}

void surface_mgr_set_calendar_toggle(struct surface_mgr *mgr, surface_mgr_launcher_fn fn, void *userdata)
{
    if (!mgr) return;
    mgr->on_calendar = fn;
    mgr->on_calendar_userdata = userdata;
}

void surface_mgr_set_launcher_toggle(struct surface_mgr *mgr, surface_mgr_launcher_fn fn, void *userdata)
{
    if (!mgr) return;
    mgr->on_launcher = fn;
    mgr->on_launcher_userdata = userdata;
}

void surface_mgr_set_control_center_toggle(struct surface_mgr *mgr, surface_mgr_launcher_fn fn, void *userdata)
{
    if (!mgr) return;
    (void)fn; (void)userdata;
}


void surface_mgr_hide_all_panels(struct surface_mgr *mgr)
{
    if (!mgr) return;
    for (int i = 0; i < mgr->n_panels; i++) {
        struct panel *p = mgr->panels[i];
        if (p && panel_is_visible(p)) {
            panel_hide(p);
        }
    }
}
