#define _GNU_SOURCE
#include "dock/dock.h"
#include "core/panel.h"
#include "core/output.h"
#include "core/wayland.h"
#include "config/config.h"
#include "launcher/desktop.h"
#include "compositors/niri_ipc.h"
#include "util/log.h"

#include <cairo.h>
#include <pango/pangocairo.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#define DOCK_MAX_ITEMS 64
#define DOCK_PAD 8.0
#define DOCK_GAP 6.0
#define DOCK_EDGE_MARGIN 6.0

struct dock_item {
    int kind;             /* 0 = launcher button, 1 = app */
    char letter[8];
    char app_id[128];     /* running window app_id, or "" */
    char launch_id[128];  /* pinned desktop id, or "" */
    char exec[512];       /* resolved exec for a pinned-but-not-running app */
    uint64_t win_id;
    bool running;
    bool focused;
    double x, w;
};

struct dock {
    struct wayland_ctx *ctx;
    struct output *out;
    struct panel panel;
    const struct config *cfg;
    struct niri_ipc *niri;
    struct launcher *launcher;
    void (*toggle)(void *);
    void *toggle_ud;
    struct desktop_db db;
    struct dock_item items[DOCK_MAX_ITEMS];
    int n_items;
    int hover;
    int width, height;
};

static void dock_spawn(const char *cmd)
{
    if (!cmd || !*cmd) return;
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(1);
    } else if (pid > 0) {
        waitpid(pid, NULL, WNOHANG);
    }
}

/* "firefox.desktop" vs window app_id "firefox" / "org.mozilla.firefox". */
static bool dock_id_matches_app(const char *id, const char *app_id)
{
    if (!id || !app_id || !*id || !*app_id) return false;
    char base[128];
    snprintf(base, sizeof(base), "%s", id);
    char *dot = strrchr(base, '.');
    if (dot && strcmp(dot, ".desktop") == 0) *dot = '\0';
    if (strcasecmp(base, app_id) == 0) return true;
    const char *last = strrchr(app_id, '.');
    last = last ? last + 1 : app_id;
    return strcasecmp(base, last) == 0;
}

static void dock_tile_letter(const char *s, char *out, size_t n)
{
    if (!s || !*s) { snprintf(out, n, "?"); return; }
    while (*s == ' ') s++;
    unsigned char c = (unsigned char)s[0];
    if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
    snprintf(out, n, "%c", c);
}

static const char *dock_pinned_exec(struct dock *d, const char *id)
{
    int idx = desktop_db_find_by_id(&d->db, id);
    return idx >= 0 ? d->db.entries[idx].exec : NULL;
}

static void dock_build_items(struct dock *d)
{
    d->n_items = 0;
    struct dock_item *it;

    /* launcher button always first */
    it = &d->items[d->n_items++];
    memset(it, 0, sizeof(*it));
    it->kind = 0;
    snprintf(it->letter, sizeof(it->letter), "\xE2\x98\xB0"); /* ☰ */

    /* pinned apps (from [dock] pinned) */
    bool used_win[NIRI_MAX_WINDOWS] = { false };
    for (int p = 0; p < d->cfg->dock.n_pinned && d->n_items < DOCK_MAX_ITEMS; p++) {
        const char *pid = d->cfg->dock.pinned[p];
        it = &d->items[d->n_items++];
        memset(it, 0, sizeof(*it));
        it->kind = 1;
        snprintf(it->launch_id, sizeof(it->launch_id), "%s", pid);
        int idx = desktop_db_find_by_id(&d->db, pid);
        if (idx >= 0) {
            snprintf(it->exec, sizeof(it->exec), "%s", d->db.entries[idx].exec);
            dock_tile_letter(d->db.entries[idx].name, it->letter, sizeof(it->letter));
        } else {
            dock_tile_letter(pid, it->letter, sizeof(it->letter));
        }
        if (d->niri) {
            for (int i = 0; i < d->niri->n_windows; i++) {
                const struct niri_window *w = &d->niri->windows[i];
                if (dock_id_matches_app(pid, w->app_id)) {
                    it->running = true;
                    it->focused = it->focused || w->focused;
                    it->win_id = w->id;
                    snprintf(it->app_id, sizeof(it->app_id), "%s", w->app_id);
                    used_win[i] = true;
                }
            }
        }
    }

    /* running windows not already represented by a pinned entry */
    if (d->niri) {
        for (int i = 0; i < d->niri->n_windows && d->n_items < DOCK_MAX_ITEMS; i++) {
            if (used_win[i]) continue;
            const struct niri_window *w = &d->niri->windows[i];
            it = &d->items[d->n_items++];
            memset(it, 0, sizeof(*it));
            it->kind = 1;
            it->running = true;
            it->focused = w->focused;
            it->win_id = w->id;
            snprintf(it->app_id, sizeof(it->app_id), "%s", w->app_id);
            dock_tile_letter(w->app_id[0] ? w->app_id : w->title, it->letter, sizeof(it->letter));
        }
    }
}

static void dock_relayout(struct dock *d)
{
    if (!d->cfg->dock.enabled) return;
    double icon = d->cfg->dock.icon_size;
    int n = d->n_items;
    int w = (int)lround(DOCK_PAD * 2 + n * icon + (n > 1 ? (n - 1) * DOCK_GAP : 0));
    if (w < 48) w = 48;
    int h = d->cfg->dock.height;
    if (w == d->width && h == d->height && panel_is_visible(&d->panel)) {
        panel_mark_dirty(&d->panel);
        return;
    }
    d->width = w;
    d->height = h;
    if (panel_is_visible(&d->panel)) {
        panel_hide(&d->panel);
    }
    panel_show_at(&d->panel, d->out, w, h, PANEL_ANCHOR_BOTTOM_CENTER);
}

static void dock_round(cairo_t *cr, double x, double y, double w, double h, double r)
{
    if (r > h / 2) r = h / 2;
    if (r > w / 2) r = w / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

static void dock_tile(cairo_t *cr, struct dock *d, const struct theme_config *th,
                      double x, double y, double size, const char *letter, bool hover,
                      bool focused, bool running)
{
    struct color_rgba bg = focused ? th->primary : th->surface_variant;
    if (hover && !focused) {
        bg.r = bg.r * 1.15f + 0.05f;
        bg.g = bg.g * 1.15f + 0.05f;
        bg.b = bg.b * 1.15f + 0.05f;
    }
    struct color_rgba fg = focused ? th->on_primary : th->foreground;

    cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, hover ? 1.0 : 0.82);
    dock_round(cr, x, y, size, size, size * 0.28);
    cairo_fill(cr);

    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, d->cfg->font.family);
    pango_font_description_set_weight(fd, PANGO_WEIGHT_BOLD);
    pango_font_description_set_absolute_size(fd, size * 0.5 * PANGO_SCALE);
    pango_layout_set_font_description(l, fd);
    pango_layout_set_text(l, letter, -1);
    int tw, tht;
    pango_layout_get_pixel_size(l, &tw, &tht);
    cairo_set_source_rgba(cr, fg.r, fg.g, fg.b, 1.0);
    cairo_move_to(cr, x + (size - tw) / 2.0, y + (size - tht) / 2.0);
    pango_cairo_show_layout(cr, l);
    pango_font_description_free(fd);
    g_object_unref(l);

    if (running) {
        struct color_rgba dot = focused ? th->primary : th->on_surface_variant;
        cairo_set_source_rgba(cr, dot.r, dot.g, dot.b, 0.9);
        cairo_arc(cr, x + size / 2.0, y + size + 3.0, 2.0, 0, 2 * G_PI);
        cairo_fill(cr);
    }
}

static void dock_draw_cb(struct panel *p, cairo_t *cr, int w, int h)
{
    struct dock *d = p->userdata;
    struct config def;
    const struct theme_config *th = d->cfg ? &d->cfg->theme : (config_defaults(&def), &def.theme);

    cairo_set_source_rgba(cr, th->panel_background.r, th->panel_background.g,
                          th->panel_background.b, 0.88);
    dock_round(cr, 0, 0, w, h, 14.0);
    cairo_fill(cr);

    double icon = d->cfg->dock.icon_size;
    double y = (h - icon) / 2.0;
    double x = DOCK_PAD;
    for (int i = 0; i < d->n_items; i++) {
        struct dock_item *it = &d->items[i];
        it->x = x;
        it->w = icon;
        dock_tile(cr, d, th, x, y, icon, it->letter, d->hover == i,
                  it->focused, it->running);
        x += icon + DOCK_GAP;
    }
}

static int dock_item_at(struct dock *d, double x)
{
    for (int i = 0; i < d->n_items; i++) {
        if (x >= d->items[i].x && x <= d->items[i].x + d->items[i].w) return i;
    }
    return -1;
}

static bool dock_click_cb(struct panel *p, double x, double y)
{
    (void)y;
    struct dock *d = p->userdata;
    int i = dock_item_at(d, x);
    if (i < 0) return false;
    struct dock_item *it = &d->items[i];
    if (it->kind == 0) {
        if (d->toggle) d->toggle(d->toggle_ud);
        log_info("dock: launcher clicked");
        return true;
    }
    if (it->running) {
        if (d->niri && niri_ipc_focus(d->niri, it->win_id))
            log_info("dock: focus window %llu", (unsigned long long)it->win_id);
        return true;
    }
    const char *exec = it->exec[0] ? it->exec : dock_pinned_exec(d, it->launch_id);
    if (exec) {
        dock_spawn(exec);
        log_info("dock: launch %s", it->launch_id);
    }
    return true;
}

static void dock_motion_cb(struct panel *p, double x, double y)
{
    (void)y;
    struct dock *d = p->userdata;
    int i = dock_item_at(d, x);
    if (i != d->hover) {
        d->hover = i;
        panel_mark_dirty(p);
    }
}

static void dock_rebuild(struct dock *d)
{
    dock_build_items(d);
    dock_relayout(d);
}

struct dock *dock_create(struct wayland_ctx *ctx, struct output *out, const struct config *cfg)
{
    if (!ctx || !out || !cfg) return NULL;
    struct dock *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->ctx = ctx;
    d->out = out;
    d->cfg = cfg;
    d->hover = -1;
    d->width = d->height = 0;
    desktop_db_init(&d->db);
    desktop_db_load_system(&d->db);
    log_info("dock: %d desktop entries", d->db.count);

    panel_init(&d->panel, ctx);
    panel_set_callbacks(&d->panel, dock_draw_cb, NULL, dock_click_cb, dock_motion_cb, d);
    panel_set_persistent(&d->panel, true);

    dock_rebuild(d);
    if (cfg->dock.enabled) dock_show(d);
    log_debug("dock created");
    return d;
}

void dock_destroy(struct dock *d)
{
    if (!d) return;
    panel_fini(&d->panel);
    desktop_db_free(&d->db);
    free(d);
}

void dock_update_config(struct dock *d, const struct config *cfg)
{
    if (!d || !cfg) return;
    d->cfg = cfg;
    if (cfg->dock.enabled) {
        dock_rebuild(d);
        dock_show(d);
    } else {
        dock_hide(d);
    }
}

void dock_show(struct dock *d)
{
    if (!d) return;
    if (!d->cfg->dock.enabled) return;
    if (!panel_is_visible(&d->panel)) dock_rebuild(d);
}

void dock_hide(struct dock *d)
{
    if (!d) return;
    panel_hide(&d->panel);
}

bool dock_is_visible(struct dock *d)
{
    return d && panel_is_visible(&d->panel);
}

struct panel *dock_surface(struct dock *d)
{
    return d ? &d->panel : NULL;
}

void dock_mark_dirty(struct dock *d)
{
    if (!d) return;
    dock_build_items(d);
    dock_relayout(d);
}

void dock_set_niri(struct dock *d, struct niri_ipc *niri)
{
    if (!d) return;
    d->niri = niri;
    dock_rebuild(d);
}

void dock_set_launcher(struct dock *d, struct launcher *launcher)
{
    if (d) d->launcher = launcher;
}

void dock_set_launcher_toggle(struct dock *d, void (*fn)(void *), void *userdata)
{
    if (!d) return;
    d->toggle = fn;
    d->toggle_ud = userdata;
}
