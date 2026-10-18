#define _GNU_SOURCE
#include "session/lock_screen.h"
#include "core/wayland.h"
#include "core/output.h"
#include "core/seat.h"
#include "config/config.h"
#include "util/log.h"
#include "ext-session-lock-v1-client-protocol.h"

#include <cairo.h>
#include <pango/pangocairo.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <dlfcn.h>

#define LOCK_MAX_SURFACES 8
#define LOCK_PW_MAX 256

/* ------------------------------------------------------------------ */
/* PAM, loaded at runtime via dlopen so there is no build dependency. */
/* ------------------------------------------------------------------ */
#define PAM_SUCCESS 0
#define PAM_PROMPT_ECHO_OFF 1
#define PAM_PROMPT_ECHO_ON 2
#define PAM_ERROR_MSG 3
#define PAM_TEXT_INFO 4

struct pam_message { int msg_style; const char *msg; };
struct pam_response { char *resp; int resp_retcode; };
struct pam_conv {
    int (*conv)(int, const struct pam_message **, struct pam_response **, void *);
    void *appdata_ptr;
};
typedef struct pam_handle pam_handle_t;
typedef int (*pam_start_fn)(const char *, const char *, const struct pam_conv *, pam_handle_t **);
typedef int (*pam_auth_fn)(pam_handle_t *, int);
typedef int (*pam_acct_fn)(pam_handle_t *, int);
typedef int (*pam_end_fn)(pam_handle_t *, int);

struct pam_api {
    void *dl;
    pam_start_fn start;
    pam_auth_fn auth;
    pam_acct_fn acct;
    pam_end_fn end;
    bool ok;
};

/* ------------------------------------------------------------------ */

struct lockbuf {
    struct wl_buffer *wl;
    void *data;
    size_t size;
    int w, h;
    bool busy;
};

struct lock_screen;

struct lock_surface {
    struct lock_screen *owner;
    struct wl_surface *surf;
    struct ext_session_lock_surface_v1 *ls;
    struct lockbuf bufs[2];
    int w, h;
    int scale;
    bool configured;
};

struct lock_screen {
    struct wayland_ctx *ctx;
    struct wl_list *outputs;
    const struct config *cfg;
    struct seat_state *seat;

    bool locked;
    bool got_locked;
    struct ext_session_lock_v1 *lock;
    struct lock_surface surfaces[LOCK_MAX_SURFACES];
    int n_surf;

    char password[LOCK_PW_MAX];
    int pw_len;
    char message[160];
    int message_kind; /* 1 = info, 2 = error */

    struct pam_api pam;
};

/* ---- buffer helpers (same scheme as the wallpaper surface) ---- */

static void buf_release(void *data, struct wl_buffer *wl)
{
    (void)wl;
    struct lockbuf *b = data;
    b->busy = false;
}

static const struct wl_buffer_listener buf_listener = { .release = buf_release };

static void buf_destroy(struct lockbuf *b)
{
    if (b->wl) wl_buffer_destroy(b->wl);
    if (b->data) munmap(b->data, b->size);
    memset(b, 0, sizeof(*b));
}

static bool buf_create(struct lock_screen *ls, struct lockbuf *b, int w, int h)
{
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
    size_t size = (size_t)stride * (size_t)h;
    int fd = memfd_create("emancipation-lock", MFD_CLOEXEC);
    if (fd < 0) return false;
    if (ftruncate(fd, (off_t)size) < 0) { close(fd); return false; }
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) { close(fd); return false; }
    struct wl_shm_pool *pool = wl_shm_create_pool(ls->ctx->shm, fd, (int32_t)size);
    b->wl = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    b->data = data;
    b->size = size;
    b->w = w;
    b->h = h;
    b->busy = false;
    wl_buffer_add_listener(b->wl, &buf_listener, b);
    return true;
}

static struct lockbuf *free_buf(struct lock_screen *ls, struct lock_surface *s, int w, int h)
{
    for (int i = 0; i < 2; i++) {
        struct lockbuf *b = &s->bufs[i];
        if (b->busy) continue;
        if (b->wl && (b->w != w || b->h != h)) buf_destroy(b);
        if (!b->wl && !buf_create(ls, b, w, h)) return NULL;
        return b;
    }
    return NULL;
}

/* ---- drawing ---- */

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

static void draw_text(cairo_t *cr, const char *font, const char *txt,
                      double cx, double y, double r, double g, double b, bool center)
{
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string(font);
    pango_layout_set_font_description(layout, fd);
    pango_layout_set_text(layout, txt, -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_move_to(cr, center ? cx - tw / 2.0 : cx, y);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(fd);
    g_object_unref(layout);
}

static int draw_surface(struct lock_surface *s)
{
    struct lock_screen *ls = s->owner;
    if (!s->configured || !s->surf || !ls->ctx->shm) return -1;
    int scale = s->scale > 0 ? s->scale : 1;
    int pw = s->w * scale, ph = s->h * scale;
    if (pw <= 0 || ph <= 0) return -1;

    struct lockbuf *b = free_buf(ls, s, pw, ph);
    if (!b) return -1;

    cairo_surface_t *cs = cairo_image_surface_create_for_data(
        b->data, CAIRO_FORMAT_ARGB32, pw, ph,
        cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, pw));
    cairo_t *cr = cairo_create(cs);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_scale(cr, scale, scale);

    const struct theme_config *th = ls->cfg ? &ls->cfg->theme : NULL;
    double br = 0.02, bg = 0.02, bb = 0.08;
    double pr = 0.12, pg = 0.12, pb = 0.12;
    double fr = 0.95, fg = 0.95, fb = 0.98;
    if (th) {
        br = th->background.r; bg = th->background.g; bb = th->background.b;
        pr = th->panel_background.r; pg = th->panel_background.g; pb = th->panel_background.b;
        fr = th->foreground.r; fg = th->foreground.g; fb = th->foreground.b;
    }
    cairo_set_source_rgb(cr, br, bg, bb);
    cairo_paint(cr);

    double w = s->w, h = s->h;
    double cw = 360, ch = 260;
    double cx = (w - cw) / 2.0, cy = (h - ch) / 2.0;
    cairo_set_source_rgba(cr, pr, pg, pb, 0.92);
    rounded_rect(cr, cx, cy, cw, ch, 22);
    cairo_fill(cr);

    char clock[16] = "";
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(clock, sizeof(clock), "%H:%M", &tm);
    static const char *wdays[7] = { "domingo", "lunes", "martes", "miercoles",
                                    "jueves", "viernes", "sabado" };
    static const char *months[12] = { "enero", "febrero", "marzo", "abril", "mayo",
                                      "junio", "julio", "agosto", "septiembre",
                                      "octubre", "noviembre", "diciembre" };
    int wd = tm.tm_wday, mo = tm.tm_mon;
    if (wd < 0 || wd > 6) wd = 0;
    if (mo < 0 || mo > 11) mo = 0;
    char date[64];
    snprintf(date, sizeof(date), "%s, %d de %s", wdays[wd], tm.tm_mday, months[mo]);

    draw_text(cr, "sans-serif Bold 56", clock, w / 2.0, cy + 26, fr, fg, fb, true);
    draw_text(cr, "sans-serif 13", date, w / 2.0, cy + 96, fr * 0.7, fg * 0.7, fb * 0.7, true);

    /* password dots */
    char dots[LOCK_PW_MAX + 1];
    int dn = ls->pw_len;
    for (int i = 0; i < dn; i++) dots[i] = '*';
    dots[dn] = '\0';
    double dot_size = ls->cfg ? (double)ls->cfg->font.size + 4 : 16;
    char dot_font[48];
    snprintf(dot_font, sizeof(dot_font), "sans-serif %.0f", dot_size);
    if (dn > 0)
        draw_text(cr, dot_font, dots, w / 2.0, cy + 150, fr, fg, fb, true);
    else
        draw_text(cr, "sans-serif 14", "Escribe tu contrasena y pulsa Enter",
                  w / 2.0, cy + 152, fr * 0.6, fg * 0.6, fb * 0.6, true);

    if (ls->message_kind == 2)
        draw_text(cr, "sans-serif 13", ls->message, w / 2.0, cy + 205, 0.97, 0.28, 0.39, true);
    else if (ls->message_kind == 1)
        draw_text(cr, "sans-serif 13", ls->message, w / 2.0, cy + 205, 0.60, 0.75, 0.98, true);

    cairo_destroy(cr);
    cairo_surface_flush(cs);
    cairo_surface_destroy(cs);

    wl_surface_set_buffer_scale(s->surf, scale);
    wl_surface_attach(s->surf, b->wl, 0, 0);
    wl_surface_damage_buffer(s->surf, 0, 0, pw, ph);
    wl_surface_commit(s->surf);
    b->busy = true;
    return 0;
}

static void redraw_all(struct lock_screen *ls)
{
    for (int i = 0; i < ls->n_surf; i++) draw_surface(&ls->surfaces[i]);
}

/* ---- session lock surface protocol ---- */

static void lss_configure(void *data, struct ext_session_lock_surface_v1 *lss,
                          uint32_t serial, uint32_t width, uint32_t height)
{
    struct lock_surface *s = data;
    ext_session_lock_surface_v1_ack_configure(lss, serial);
    if (width > 0) s->w = (int)width;
    if (height > 0) s->h = (int)height;
    s->configured = true;
    draw_surface(s);
}

static const struct ext_session_lock_surface_v1_listener lss_listener = {
    .configure = lss_configure,
};

/* ---- session lock protocol ---- */

static void ls_locked(void *data, struct ext_session_lock_v1 *lock)
{
    (void)lock;
    struct lock_screen *ls = data;
    ls->got_locked = true;
    ls->locked = true;
    log_info("session locked");
    redraw_all(ls);
}

static void ls_finished(void *data, struct ext_session_lock_v1 *lock)
{
    (void)lock;
    struct lock_screen *ls = data;
    log_info("session lock finished by compositor");
    /* The compositor dismissed the lock: tear down without unlock request. */
    if (ls->lock) {
        ext_session_lock_v1_destroy(ls->lock);
        ls->lock = NULL;
    }
    for (int i = 0; i < ls->n_surf; i++) {
        struct lock_surface *s = &ls->surfaces[i];
        if (s->ls) { ext_session_lock_surface_v1_destroy(s->ls); s->ls = NULL; }
        if (s->surf) { wl_surface_destroy(s->surf); s->surf = NULL; }
        buf_destroy(&s->bufs[0]);
        buf_destroy(&s->bufs[1]);
        s->configured = false;
    }
    ls->n_surf = 0;
    ls->got_locked = false;
    ls->locked = false;
}

static const struct ext_session_lock_v1_listener ls_listener = {
    .locked = ls_locked,
    .finished = ls_finished,
};

/* ---- PAM ---- */

static void pam_load(struct pam_api *p)
{
    memset(p, 0, sizeof(*p));
    p->dl = dlopen("libpam.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (!p->dl) p->dl = dlopen("libpam.so", RTLD_LAZY | RTLD_LOCAL);
    if (!p->dl) {
        log_warn("lock: libpam not available: %s", dlerror());
        return;
    }
    p->start = (pam_start_fn)dlsym(p->dl, "pam_start");
    p->auth = (pam_auth_fn)dlsym(p->dl, "pam_authenticate");
    p->acct = (pam_acct_fn)dlsym(p->dl, "pam_acct_mgmt");
    p->end = (pam_end_fn)dlsym(p->dl, "pam_end");
    p->ok = p->start && p->auth && p->end;
    if (!p->ok) log_warn("lock: libpam missing symbols");
    else log_info("lock: PAM ready");
}

static int pam_conv_cb(int num_msg, const struct pam_message **msg,
                       struct pam_response **resp, void *appdata)
{
    struct lock_screen *ls = appdata;
    if (num_msg <= 0 || !resp) return PAM_SUCCESS;
    struct pam_response *r = calloc((size_t)num_msg, sizeof(*r));
    if (!r) return 5; /* PAM_BUF_ERR */
    for (int i = 0; i < num_msg; i++) {
        int st = msg[i] ? msg[i]->msg_style : PAM_ERROR_MSG;
        if (st == PAM_PROMPT_ECHO_OFF || st == PAM_PROMPT_ECHO_ON)
            r[i].resp = strdup(ls->password);
        else
            r[i].resp = NULL;
        r[i].resp_retcode = 0;
    }
    *resp = r;
    return PAM_SUCCESS;
}

static bool pam_authenticate_password(struct lock_screen *ls, const char *user)
{
    if (!ls->pam.ok) return false;
    struct pam_conv conv = { pam_conv_cb, ls };
    pam_handle_t *ph = NULL;
    int rc = ls->pam.start("login", user, &conv, &ph);
    if (rc != PAM_SUCCESS) { log_warn("lock: pam_start rc=%d", rc); return false; }
    rc = ls->pam.auth(ph, 0);
    if (rc == PAM_SUCCESS && ls->pam.acct) rc = ls->pam.acct(ph, 0);
    ls->pam.end(ph, rc);
    log_info("lock: pam_authenticate rc=%d", rc);
    return rc == PAM_SUCCESS;
}

/* ---- input ---- */

static bool lock_key_cb(const char *utf8, uint32_t keysym, uint32_t mods, void *userdata)
{
    (void)mods;
    struct lock_screen *ls = userdata;
    if (!ls->locked) return false;

    if (keysym == XKB_KEY_Return || keysym == XKB_KEY_KP_Enter) {
        if (ls->pw_len > 0) {
            const char *user = getenv("USER");
            if (!user || !user[0]) user = "root";
            if (pam_authenticate_password(ls, user)) {
                lock_screen_unlock(ls);
                return true;
            }
            snprintf(ls->message, sizeof(ls->message), "Contrasena incorrecta");
            ls->message_kind = 2;
            ls->pw_len = 0;
            ls->password[0] = '\0';
        }
    } else if (keysym == XKB_KEY_BackSpace) {
        if (ls->pw_len > 0) ls->password[--ls->pw_len] = '\0';
    } else if (keysym == XKB_KEY_Escape) {
        ls->pw_len = 0;
        ls->password[0] = '\0';
        ls->message_kind = 0;
    } else if (utf8 && utf8[0]) {
        unsigned char c0 = (unsigned char)utf8[0];
        if (c0 >= 0x20 && c0 != 0x7f) {
            size_t n = strlen(utf8);
            if (ls->pw_len + (int)n < LOCK_PW_MAX - 1) {
                memcpy(ls->password + ls->pw_len, utf8, n);
                ls->pw_len += (int)n;
                ls->password[ls->pw_len] = '\0';
            }
        }
    }
    redraw_all(ls);
    return true;
}

/* ---- lifecycle ---- */

struct lock_screen *lock_screen_create(struct wayland_ctx *ctx, struct wl_list *outputs)
{
    if (!ctx) return NULL;
    struct lock_screen *ls = calloc(1, sizeof(*ls));
    if (!ls) return NULL;
    ls->ctx = ctx;
    ls->outputs = outputs;
    ls->seat = ctx->seat;
    pam_load(&ls->pam);
    log_debug("lock screen created");
    return ls;
}

void lock_screen_set_config(struct lock_screen *ls, const struct config *cfg)
{
    if (!ls) return;
    ls->cfg = cfg;
    if (ls->locked) redraw_all(ls);
}

static struct lock_surface *add_lock_surface(struct lock_screen *ls, struct output *out)
{
    if (ls->n_surf >= LOCK_MAX_SURFACES) return NULL;
    struct lock_surface *s = &ls->surfaces[ls->n_surf];
    memset(s, 0, sizeof(*s));
    s->owner = ls;
    s->scale = out->scale > 0 ? out->scale : 1;
    s->w = out->w > 0 ? out->w : 1920;
    s->h = out->h > 0 ? out->h : 1080;
    s->surf = wl_compositor_create_surface(ls->ctx->compositor);
    if (!s->surf) return NULL;
    s->ls = ext_session_lock_v1_get_lock_surface(ls->lock, s->surf, out->wl);
    ext_session_lock_surface_v1_add_listener(s->ls, &lss_listener, s);
    ls->n_surf++;
    return s;
}

void lock_screen_lock(struct lock_screen *ls)
{
    if (!ls || ls->locked) return;
    if (ls->cfg && !ls->cfg->lock.enabled) {
        log_info("lock: disabled by config, ignoring lock request");
        return;
    }
    if (!ls->ctx->session_lock_mgr) {
        log_warn("lock: ext_session_lock_manager_v1 not available");
        return;
    }
    ls->pw_len = 0;
    ls->password[0] = '\0';
    ls->message[0] = '\0';
    ls->message_kind = 0;

    ls->lock = ext_session_lock_manager_v1_lock(ls->ctx->session_lock_mgr);
    ext_session_lock_v1_add_listener(ls->lock, &ls_listener, ls);

    struct output *out;
    wl_list_for_each(out, ls->outputs, link) {
        add_lock_surface(ls, out);
    }

    /* Route all key input to the password field while locked. */
    if (ls->seat) seat_set_text_handler(ls->seat, lock_key_cb, ls);
    ls->locked = true;
    log_info("session lock requested (%d surfaces)", ls->n_surf);
}

void lock_screen_unlock(struct lock_screen *ls)
{
    if (!ls || !ls->locked) return;

    if (ls->seat) seat_set_text_handler(ls->seat, NULL, NULL);

    if (ls->lock) {
        if (ls->got_locked)
            ext_session_lock_v1_unlock_and_destroy(ls->lock);
        else
            ext_session_lock_v1_destroy(ls->lock);
        ls->lock = NULL;
    }
    for (int i = 0; i < ls->n_surf; i++) {
        struct lock_surface *s = &ls->surfaces[i];
        if (s->ls) { ext_session_lock_surface_v1_destroy(s->ls); s->ls = NULL; }
        if (s->surf) { wl_surface_destroy(s->surf); s->surf = NULL; }
        buf_destroy(&s->bufs[0]);
        buf_destroy(&s->bufs[1]);
        s->configured = false;
    }
    ls->n_surf = 0;
    ls->got_locked = false;
    ls->locked = false;
    ls->pw_len = 0;
    ls->password[0] = '\0';
    ls->message_kind = 0;
    log_info("session unlocked");
}

void lock_screen_refresh(struct lock_screen *ls)
{
    if (ls && ls->locked) redraw_all(ls);
}

bool lock_screen_is_locked(struct lock_screen *ls)
{
    return ls ? ls->locked : false;
}

bool lock_screen_enabled(struct lock_screen *ls)
{
    if (!ls || !ls->cfg) return true;
    return ls->cfg->lock.enabled;
}

void lock_screen_destroy(struct lock_screen *ls)
{
    if (!ls) return;
    if (ls->locked) lock_screen_unlock(ls);
    if (ls->pam.dl) dlclose(ls->pam.dl);
    free(ls);
}
