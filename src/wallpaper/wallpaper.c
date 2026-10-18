#define _GNU_SOURCE
#include "wallpaper/wallpaper.h"
#include "core/output.h"
#include "core/wayland.h"
#include "zwlr-layer-shell-v1-client-protocol.h"
#include "util/log.h"
#include "third_party/stb/stb_image.h"

#include <cairo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <stdint.h>
static int strnatcasecmp_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = tolower((unsigned char)*a);
        char cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static bool is_image_ext(const char *name) {
    const char *p = strrchr(name, '.');
    if (!p) return false;
    const char *ext = p + 1;
    char e[16];
    int i = 0;
    for (; i < 15 && ext[i]; i++) e[i] = tolower((unsigned char)ext[i]);
    e[i] = '\0';
    if (strcmp(e, "png") == 0 || strcmp(e, "jpg") == 0 || strcmp(e, "jpeg") == 0 ||
        strcmp(e, "bmp") == 0 || strcmp(e, "tga") == 0 || strcmp(e, "webp") == 0 ||
        strcmp(e, "gif") == 0 || strcmp(e, "svg") == 0) return true;
    return false;
}

static int cmp_img(const void *a, const void *b) {
    const char *sa = *(const char **)a;
    const char *sb = *(const char **)b;
    return strnatcasecmp_ci(sa, sb);
}

#include <sys/mman.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>

struct wp_buf {
    struct wl_buffer *wl;
    void *data;
    size_t size;
    int width;
    int height;
    bool busy;
};

struct wallpaper {
    struct wayland_ctx *ctx;
    struct output *out;
    char path[512];
    char dir[512];
    enum wp_fill_mode fill;
    bool enabled;

    struct wl_surface *surf;
    struct zwlr_layer_surface_v1 *ls;
    struct wp_buf bufs[2];
    int w, h;             /* logical size */
    bool configured;
    bool visible;

    cairo_surface_t *image; /* decoded source, cached */
    char image_path[512];

    /* Crossfade between wallpapers. */
    cairo_surface_t *prev;      /* outgoing image, freed when the fade ends */
    bool animating;
    long long anim_start_ms;
    int transition_ms;
    struct wl_callback *frame_cb;

    /* Solid color painted behind/without an image. */
    double fb_r, fb_g, fb_b;
};

static void wp_buf_release(void *data, struct wl_buffer *wl)
{
    (void)wl;
    struct wp_buf *b = data;
    b->busy = false;
}

static const struct wl_buffer_listener wp_buf_listener = {
    .release = wp_buf_release,
};

static void wp_buf_destroy(struct wp_buf *b)
{
    if (b->wl) wl_buffer_destroy(b->wl);
    if (b->data) munmap(b->data, b->size);
    memset(b, 0, sizeof(*b));
}

static bool wp_buf_create(struct wl_shm *shm, struct wp_buf *b, int w, int h)
{
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
    size_t size = (size_t)stride * (size_t)h;
    int fd = memfd_create("emancipation-wallpaper", MFD_CLOEXEC);
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
    wl_buffer_add_listener(b->wl, &wp_buf_listener, b);
    return true;
}

static struct wp_buf *wp_free_buf(struct wallpaper *wp, int w, int h)
{
    for (int i = 0; i < 2; i++) {
        struct wp_buf *b = &wp->bufs[i];
        if (b->busy) continue;
        if (b->wl && (b->width != w || b->height != h)) wp_buf_destroy(b);
        if (!b->wl && !wp_buf_create(wp->ctx->shm, b, w, h)) return NULL;
        return b;
    }
    return NULL;
}

static long long wp_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Decode any raster format stb_image understands (png/jpg/bmp/tga/gif/psd/
 * hdr/pnm) into a cairo ARGB32 surface. Returns NULL on failure. */
static cairo_surface_t *wp_decode_stb(const char *path)
{
    int iw = 0, ih = 0, n = 0;
    stbi_uc *pix = stbi_load(path, &iw, &ih, &n, 4);
    if (!pix) return NULL;
    if (iw <= 0 || ih <= 0) { stbi_image_free(pix); return NULL; }
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(s);
        stbi_image_free(pix);
        return NULL;
    }
    unsigned char *data = cairo_image_surface_get_data(s);
    int stride = cairo_image_surface_get_stride(s);
    for (int y = 0; y < ih; y++) {
        uint32_t *dst = (uint32_t *)(data + (size_t)y * stride);
        stbi_uc *src = pix + (size_t)y * iw * 4;
        for (int x = 0; x < iw; x++) {
            uint32_t a = src[x * 4 + 3];
            /* cairo ARGB32 is premultiplied. */
            uint32_t r = (uint32_t)src[x * 4 + 0] * a / 255u;
            uint32_t g = (uint32_t)src[x * 4 + 1] * a / 255u;
            uint32_t b = (uint32_t)src[x * 4 + 2] * a / 255u;
            dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    cairo_surface_mark_dirty(s);
    stbi_image_free(pix);
    return s;
}

static cairo_surface_t *wp_load_image(struct wallpaper *wp)
{
    if (wp->image && strcmp(wp->image_path, wp->path) == 0) return wp->image;
    if (wp->image) { cairo_surface_destroy(wp->image); wp->image = NULL; }
    wp->image_path[0] = '\0';
    if (!wp->path[0]) return NULL;

    cairo_surface_t *s = wp_decode_stb(wp->path);
    if (!s) {
        /* Fall back to cairo's built-in PNG reader. */
        cairo_surface_t *png = cairo_image_surface_create_from_png(wp->path);
        if (cairo_surface_status(png) == CAIRO_STATUS_SUCCESS) {
            s = png;
        } else {
            cairo_surface_destroy(png);
        }
    }
    if (!s) {
        log_warn("wallpaper: cannot load %s", wp->path);
        return NULL;
    }
    wp->image = s;
    snprintf(wp->image_path, sizeof(wp->image_path), "%s", wp->path);
    log_info("wallpaper loaded %s (%dx%d)", wp->path,
             cairo_image_surface_get_width(s), cairo_image_surface_get_height(s));
    return wp->image;
}

static void wp_blit(cairo_t *cr, int w, int h, cairo_surface_t *img, enum wp_fill_mode fill)
{
    if (!img) return;

    double iw = cairo_image_surface_get_width(img);
    double ih = cairo_image_surface_get_height(img);
    if (iw <= 0 || ih <= 0) return;

    double sx = w / iw, sy = h / ih;
    double scale;
    switch (fill) {
    case WP_FILL_STRETCH:
        cairo_save(cr);
        cairo_scale(cr, sx, sy);
        cairo_set_source_surface(cr, img, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
        return;
    case WP_FILL_FIT:
        scale = sx < sy ? sx : sy;
        break;
    case WP_FILL_CENTER:
        scale = 1.0;
        break;
    case WP_FILL_REPEAT: {
        cairo_pattern_t *pat = cairo_pattern_create_for_surface(img);
        cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
        cairo_set_source(cr, pat);
        cairo_paint(cr);
        cairo_pattern_destroy(pat);
        return;
    }
    case WP_FILL_SPAN:
    case WP_FILL_CROP:
    default:
        scale = sx > sy ? sx : sy;
        break;
    }
    cairo_save(cr);
    cairo_translate(cr, (w - iw * scale) / 2.0, (h - ih * scale) / 2.0);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, img, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
}

static void wp_blit_alpha(cairo_t *cr, int w, int h, cairo_surface_t *img,
                          enum wp_fill_mode fill, double alpha)
{
    if (!img || alpha <= 0.0) return;
    if (alpha >= 1.0) { wp_blit(cr, w, h, img, fill); return; }
    cairo_push_group(cr);
    wp_blit(cr, w, h, img, fill);
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, alpha);
}

static void wp_frame_done(void *data, struct wl_callback *cb, uint32_t time);

static const struct wl_callback_listener wp_frame_listener = {
    .done = wp_frame_done,
};

static void wp_draw(struct wallpaper *wp)
{
    if (!wp->configured || !wp->surf || !wp->ctx->shm) return;
    int scale = (wp->out && wp->out->scale > 0) ? wp->out->scale : 1;
    int pw = wp->w * scale;
    int ph = wp->h * scale;
    if (pw <= 0 || ph <= 0) return;

    struct wp_buf *b = wp_free_buf(wp, pw, ph);
    if (!b) return;

    cairo_surface_t *cs = cairo_image_surface_create_for_data(
        b->data, CAIRO_FORMAT_ARGB32, pw, ph,
        cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, pw));
    cairo_t *cr = cairo_create(cs);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_scale(cr, scale, scale);

    cairo_set_source_rgb(cr, wp->fb_r, wp->fb_g, wp->fb_b);
    cairo_paint(cr);

    cairo_surface_t *cur = wp_load_image(wp);

    double t = 1.0;
    if (wp->animating && wp->transition_ms > 0) {
        long long el = wp_now_ms() - wp->anim_start_ms;
        t = (double)el / (double)wp->transition_ms;
        if (t > 1.0) t = 1.0;
        if (t < 0.0) t = 0.0;
    }

    if (wp->animating && wp->prev)
        wp_blit(cr, wp->w, wp->h, wp->prev, wp->fill);
    wp_blit_alpha(cr, wp->w, wp->h, cur, wp->fill, t);

    if (wp->animating && t >= 1.0) {
        wp->animating = false;
        if (wp->prev) { cairo_surface_destroy(wp->prev); wp->prev = NULL; }
    }
    cairo_destroy(cr);
    cairo_surface_flush(cs);
    cairo_surface_destroy(cs);

    wl_surface_set_buffer_scale(wp->surf, scale);
    if (wp->animating) {
        wp->frame_cb = wl_surface_frame(wp->surf);
        wl_callback_add_listener(wp->frame_cb, &wp_frame_listener, wp);
    }
    wl_surface_attach(wp->surf, b->wl, 0, 0);
    wl_surface_damage_buffer(wp->surf, 0, 0, pw, ph);
    wl_surface_commit(wp->surf);
    b->busy = true;
}

static void wp_frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    (void)time;
    struct wallpaper *wp = data;
    if (wp->frame_cb == cb) wp->frame_cb = NULL;
    wl_callback_destroy(cb);
    if (wp->animating && wp->visible) wp_draw(wp);
}

static void wp_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial,
                         uint32_t w, uint32_t h)
{
    struct wallpaper *wp = data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    if (w > 0) wp->w = (int)w;
    if (h > 0) wp->h = (int)h;
    wp->configured = true;
    wp->visible = true;
    wp_draw(wp);
}

static void wp_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
    (void)ls;
    struct wallpaper *wp = data;
    log_info("wallpaper closed by compositor");
    wp->configured = false;
    wp->visible = false;
}

static const struct zwlr_layer_surface_v1_listener wp_ls_listener = {
    .configure = wp_configure,
    .closed = wp_closed,
};

static void wp_teardown(struct wallpaper *wp)
{
    if (wp->frame_cb) { wl_callback_destroy(wp->frame_cb); wp->frame_cb = NULL; }
    if (wp->ls) { zwlr_layer_surface_v1_destroy(wp->ls); wp->ls = NULL; }
    if (wp->surf) { wl_surface_destroy(wp->surf); wp->surf = NULL; }
    wp_buf_destroy(&wp->bufs[0]);
    wp_buf_destroy(&wp->bufs[1]);
    if (wp->prev) { cairo_surface_destroy(wp->prev); wp->prev = NULL; }
    wp->animating = false;
    wp->configured = false;
    wp->visible = false;
}

static bool wp_create_surface(struct wallpaper *wp)
{
    if (!wp->ctx || !wp->ctx->layer_shell || !wp->ctx->compositor || !wp->ctx->shm) return false;
    if (wp->surf) return true;
    if (!wp->out) return false;
    wp->surf = wl_compositor_create_surface(wp->ctx->compositor);
    if (!wp->surf) return false;
    wp->ls = zwlr_layer_shell_v1_get_layer_surface(wp->ctx->layer_shell, wp->surf, wp->out->wl,
                                                   ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
                                                   "emancipation-wallpaper");
    zwlr_layer_surface_v1_add_listener(wp->ls, &wp_ls_listener, wp);
    zwlr_layer_surface_v1_set_anchor(wp->ls, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                             ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                             ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                             ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(wp->ls, -1);
    zwlr_layer_surface_v1_set_size(wp->ls, 0, 0);
    wl_surface_commit(wp->surf);
    return true;
}

struct wallpaper *wallpaper_create(struct wayland_ctx *ctx, struct output *out)
{
    if (!ctx || !out) return NULL;
    struct wallpaper *wp = calloc(1, sizeof(*wp));
    if (!wp) return NULL;
    wp->ctx = ctx;
    wp->out = out;
    wp->path[0] = '\0';
    wp->fill = WP_FILL_CROP;
    wp->transition_ms = 700;
    wp->fb_r = 0.0; wp->fb_g = 0.0; wp->fb_b = 0.0;
    log_debug("wallpaper created");
    return wp;
}

void wallpaper_destroy(struct wallpaper *wp)
{
    if (!wp) return;
    wp_teardown(wp);
    if (wp->frame_cb) { wl_callback_destroy(wp->frame_cb); wp->frame_cb = NULL; }
    if (wp->image) cairo_surface_destroy(wp->image);
    if (wp->prev) cairo_surface_destroy(wp->prev);
    free(wp);
}

void wallpaper_set_path(struct wallpaper *wp, const char *path)
{
    if (!wp || !path) return;
    if (strcmp(wp->path, path) == 0) return;
    strncpy(wp->path, path, sizeof(wp->path) - 1);
    wp->path[sizeof(wp->path) - 1] = '\0';
    const char *slash = strrchr(wp->path, '/');
    if (slash && slash != wp->path) {
        size_t n = (size_t)(slash - wp->path);
        if (n >= sizeof(wp->dir)) n = sizeof(wp->dir) - 1;
        memcpy(wp->dir, wp->path, n);
        wp->dir[n] = '\0';
    } else if (slash == wp->path) {
        snprintf(wp->dir, sizeof(wp->dir), "/");
    } else {
        wp->dir[0] = '\0';
    }
    log_info("wallpaper set: %s", wp->path);

    if (wp->visible && wp->image && wp->transition_ms > 0) {
        /* Crossfade: keep the current image as the outgoing layer. */
        if (wp->prev) cairo_surface_destroy(wp->prev);
        wp->prev = wp->image;
        wp->image = NULL;
        wp->image_path[0] = '\0';
        wp->animating = true;
        wp->anim_start_ms = wp_now_ms();
    } else {
        if (wp->image) { cairo_surface_destroy(wp->image); wp->image = NULL; }
        wp->image_path[0] = '\0';
        wp->animating = false;
    }
    if (wp->visible) wp_draw(wp);
}

void wallpaper_set_fill(struct wallpaper *wp, enum wp_fill_mode fill)
{
    if (!wp || wp->fill == fill) return;
    wp->fill = fill;
    log_info("wallpaper fill: %s", wp_fill_to_str(fill));
    if (wp->visible) wp_draw(wp);
}

/* Point the wallpaper browser at a directory: either a directory itself, or
 * the parent of a given file path. Requires gate on the filesystem. */
void wallpaper_set_dir(struct wallpaper *wp, const char *dir)
{
    if (!wp) return;
    if (!dir || !dir[0]) { wp->dir[0] = '\0'; return; }
    char tmp[512];
    struct stat st;
    if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        snprintf(tmp, sizeof(tmp), "%s", dir);
    } else {
        const char *slash = strrchr(dir, '/');
        if (!slash || slash == dir) return;
        snprintf(tmp, sizeof(tmp), "%.*s", (int)(slash - dir), dir);
    }
    snprintf(wp->dir, sizeof(wp->dir), "%s", tmp);
}

/* The directory used by next/prev/random: explicit dir if set, else the
 * directory that contains the current wallpaper path. */
static char *dirname_of(const char *path);
static const char *wp_effective_dir(struct wallpaper *wp, char *buf, size_t n)
{
    if (wp->dir[0]) {
        snprintf(buf, n, "%s", wp->dir);
    } else {
        const char *cur = wp->path[0] ? wp->path : NULL;
        char *d = dirname_of(cur);
        if (!d) return NULL;
        snprintf(buf, n, "%s", d);
        free(d);
    }
    return buf;
}


static void collect_images(const char *base, char ***out_list, int *out_n, int *out_cap, bool recurse) {
    DIR *d = opendir(base);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", base, de->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (recurse) collect_images(full, out_list, out_n, out_cap, recurse);
        } else if (S_ISREG(st.st_mode)) {
            if (is_image_ext(de->d_name)) {
                if (*out_n >= *out_cap) {
                    *out_cap = (*out_cap) * 2 + 64;
                    *out_list = (char **)realloc(*out_list, (*out_cap) * sizeof(char *));
                }
                (*out_list)[(*out_n)++] = strdup(full);
            }
        }
    }
    closedir(d);
}

static void free_images(char **list, int n) {
    for (int i = 0; i < n; i++) free(list[i]);
    free(list);
}

static char *dirname_of(const char *path) {
    if (!path || path[0] == '\0') return NULL;
    char *cpy = strdup(path);
    char *p = strrchr(cpy, '/');
    if (!p) { free(cpy); return NULL; }
    if (p == cpy) { free(cpy); return strdup("/"); }
    *p = '\0';
    char *d = strdup(cpy);
    free(cpy);
    return d;
}

void wallpaper_set_enabled(struct wallpaper *wp, bool on)
{
    if (!wp) return;
    if (on == wp->enabled) return;
    wp->enabled = on;
    if (on) {
        if (wp_create_surface(wp)) log_info("wallpaper enabled");
    } else {
        wp_teardown(wp);
        log_info("wallpaper disabled");
    }
}

const char *wallpaper_get_path(const struct wallpaper *wp)
{
    return wp ? wp->path : "";
}

const char *wallpaper_get_dir(const struct wallpaper *wp)
{
    return wp ? wp->dir : "";
}

/* When no wallpaper path is configured, pick the first image found in the
 * explicit dir (if any) or in the standard system directories. */
void wallpaper_autodetect(struct wallpaper *wp)
{
    if (!wp || wp->path[0]) return;
    const char *dirs[3];
    int nd = 0;
    if (wp->dir[0]) dirs[nd++] = wp->dir;
    dirs[nd++] = "/usr/share/wallpapers";
    dirs[nd++] = "/usr/share/backgrounds";
    for (int i = 0; i < nd; i++) {
        char **list = NULL;
        int n = 0, cap = 0;
        collect_images(dirs[i], &list, &n, &cap, true);
        if (n > 0) {
            qsort(list, n, sizeof(char *), cmp_img);
            wallpaper_set_path(wp, list[0]);
            free_images(list, n);
            return;
        }
        free_images(list, n);
    }
}

enum wp_fill_mode wallpaper_get_fill(const struct wallpaper *wp)
{
    return wp ? wp->fill : WP_FILL_CROP;
}

void wallpaper_reload(struct wallpaper *wp)
{
    if (!wp) return;
    if (wp->image) { cairo_surface_destroy(wp->image); wp->image = NULL; }
    if (wp->prev) { cairo_surface_destroy(wp->prev); wp->prev = NULL; }
    wp->image_path[0] = '\0';
    wp->animating = false;
    if (wp->visible) wp_draw(wp);
    log_info("wallpaper reload");
}

void wallpaper_set_fallback(struct wallpaper *wp, double r, double g, double b)
{
    if (!wp) return;
    wp->fb_r = r; wp->fb_g = g; wp->fb_b = b;
    if (wp->visible) wp_draw(wp);
}

void wallpaper_set_transition(struct wallpaper *wp, int ms)
{
    if (!wp) return;
    if (ms < 0) ms = 0;
    wp->transition_ms = ms;
}


void wallpaper_next(struct wallpaper *wp) {
    if (!wp) return;
    const char *cur = wp->path[0] ? wp->path : NULL;
    char dirstr[512];
    const char *dir = wp_effective_dir(wp, dirstr, sizeof(dirstr));
    if (!dir) return;
    char **list = NULL;
    int n = 0, cap = 0;
    collect_images(dir, &list, &n, &cap, true);
    if (n == 0) {
        free_images(list, n);
        return;
    }
    qsort(list, n, sizeof(char *), cmp_img);
    int idx = -1;
    if (cur) {
        for (int i = 0; i < n; i++) {
            if (strcmp(list[i], cur) == 0) { idx = i; break; }
        }
    }
    int next_idx = (idx == -1 || idx + 1 >= n) ? 0 : idx + 1;
    wallpaper_set_path(wp, list[next_idx]);
    free_images(list, n);
}

void wallpaper_prev(struct wallpaper *wp) {
    if (!wp) return;
    const char *cur = wp->path[0] ? wp->path : NULL;
    char dirstr[512];
    const char *dir = wp_effective_dir(wp, dirstr, sizeof(dirstr));
    if (!dir) return;
    char **list = NULL;
    int n = 0, cap = 0;
    collect_images(dir, &list, &n, &cap, true);
    if (n == 0) {
        free_images(list, n);
        return;
    }
    qsort(list, n, sizeof(char *), cmp_img);
    int idx = -1;
    if (cur) {
        for (int i = 0; i < n; i++) {
            if (strcmp(list[i], cur) == 0) { idx = i; break; }
        }
    }
    int prev_idx = (idx <= 0) ? n - 1 : idx - 1;
    wallpaper_set_path(wp, list[prev_idx]);
    free_images(list, n);
}

void wallpaper_random(struct wallpaper *wp) {
    if (!wp) return;
    const char *cur = wp->path[0] ? wp->path : NULL;
    char dirstr[512];
    const char *dir = wp_effective_dir(wp, dirstr, sizeof(dirstr));
    if (!dir) return;
    char **list = NULL;
    int n = 0, cap = 0;
    collect_images(dir, &list, &n, &cap, true);
    if (n == 0) {
        free_images(list, n);
        return;
    }
    qsort(list, n, sizeof(char *), cmp_img);
    if (n == 1) {
        wallpaper_set_path(wp, list[0]);
        free_images(list, n);
        return;
    }
    int idx = -1;
    if (cur) {
        for (int i = 0; i < n; i++) {
            if (strcmp(list[i], cur) == 0) { idx = i; break; }
        }
    }
    int cand = idx;
    unsigned int seed = (unsigned int)time(NULL);
    while (cand == idx) {
        cand = rand_r(&seed) % n;
    }
    wallpaper_set_path(wp, list[cand]);
    free_images(list, n);
}
