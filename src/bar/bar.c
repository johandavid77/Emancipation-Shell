#include "bar/bar.h"
#include "bar/sysinfo.h"
#include "bar/workspaces.h"
#include "launcher/launcher.h"
#include "launcher/desktop.h"
#include "launcher/icons.h"
#include "compositors/niri_ipc.h"
#include "config/config.h"
#include <pango/pangocairo.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Workspace pill proportions follow Noctalia's "regular" style
 * (workspaces_widget.h/.cpp): active pill 2.2x the base, inactive 1.0x,
 * 4px gap, label text ~0.8 of body text. */
#define WS_GAP 4.0
#define WS_ACTIVE_MUL 2.2
#define WS_LABEL_PAD 6.0
#define WS_EMPTY_ALPHA 0.55

struct draw_env {
    cairo_t *cr;
    PangoLayout *text;  /* body font */
    PangoLayout *small; /* workspace labels */
    const struct bar_ctx *ctx;
    const struct theme_config *th;
    int h;
    struct bar_hits *hits; /* NULL during the measure pass */
};

static void set_rgba(cairo_t *cr, struct color_rgba c, double alpha_mul)
{
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a * alpha_mul);
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
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

static double text_width(PangoLayout *l, const char *txt)
{
    int tw, th;
    pango_layout_set_text(l, txt, -1);
    pango_layout_get_pixel_size(l, &tw, &th);
    return tw;
}

static void text_draw(struct draw_env *e, PangoLayout *l, const char *txt, double x)
{
    int tw, th;
    pango_layout_set_text(l, txt, -1);
    pango_layout_get_pixel_size(l, &tw, &th);
    cairo_move_to(e->cr, x, (e->h - th) / 2.0);
    pango_cairo_show_layout(e->cr, l);
}

static void add_hit(struct draw_env *e, double x0, double x1, enum bar_hit_kind k, void *ref)
{
    if (!e->hits || e->hits->n >= BAR_MAX_HITS) return;
    e->hits->h[e->hits->n++] = (struct bar_hit){ x0, x1, k, ref };
}

static bool hovered(const struct draw_env *e, double x0, double x1)
{
    return e->ctx->hover_x >= x0 && e->ctx->hover_x < x1;
}

/* ---- widgets: each returns its width; draws only when draw != 0 -------- */

static double w_workspaces(struct draw_env *e, double x, int draw)
{
    struct workspace_info ws[32];
    size_t n = workspaces_snapshot(e->ctx->wm, e->ctx->output, ws, 32);
    double pill_h = e->h * 0.56;
    double y = (e->h - pill_h) / 2.0;
    double cx = x;
    for (size_t i = 0; i < n; i++) {
        const char *label = ws[i].label[0] ? ws[i].label : "?";
        double tw = text_width(e->small, label);
        double base = pill_h > tw + 2 * WS_LABEL_PAD ? pill_h : tw + 2 * WS_LABEL_PAD;
        double pw = ws[i].active ? pill_h * WS_ACTIVE_MUL : base;
        if (pw < tw + 2 * WS_LABEL_PAD) pw = tw + 2 * WS_LABEL_PAD;
        if (draw) {
            struct color_rgba fill, ink;
            double alpha = 1.0;
            if (ws[i].active) {
                fill = e->th->primary;
                ink = e->th->on_primary;
            } else if (ws[i].urgent) {
                fill = e->th->error;
                ink = e->th->on_error;
            } else {
                fill = e->th->secondary;
                ink = e->th->on_secondary;
                alpha = ws[i].hidden ? WS_EMPTY_ALPHA : 0.8;
            }
            if (hovered(e, cx, cx + pw) && !ws[i].active) alpha = 1.0;
            set_rgba(e->cr, fill, alpha);
            rounded_rect(e->cr, cx, y, pw, pill_h, pill_h / 2);
            cairo_fill(e->cr);
            set_rgba(e->cr, ink, 1.0);
            text_draw(e, e->small, label, cx + (pw - tw) / 2.0);
            add_hit(e, cx, cx + pw, BAR_HIT_WORKSPACE, ws[i].ref);
        }
        cx += pw + (i + 1 < n ? WS_GAP : 0);
    }
    return cx - x;
}

static double w_time(struct draw_env *e, double x, int draw, const char *fmt, double alpha, enum bar_hit_kind hit)
{
    char buf[128];
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    if (strftime(buf, sizeof(buf), fmt, &tmv) == 0) buf[0] = '\0';
    double tw = text_width(e->text, buf);
    if (draw) {
        set_rgba(e->cr, e->th->foreground, alpha);
        text_draw(e, e->text, buf, x);
        add_hit(e, x - 8, x + tw + 8, hit, NULL);
    }
    return tw;
}

/* Taskbar: one icon per mapped toplevel, focused one highlighted.
 * Window data comes from niri IPC, the same source Noctalia uses on niri. */
static double w_taskbar(struct draw_env *e, double x, int draw)
{
    struct niri_ipc *n = e->ctx->niri;
    if (!n) return 0.0;
    int count = niri_ipc_window_count(n);
    if (count <= 0) return 0.0;
    double icon_px = 20.0;
    double gap = 4.0;
    double total = count * (icon_px + gap) - gap;
    if (!draw) return total;
    for (int i = 0; i < count; i++) {
        const struct niri_window *w = niri_ipc_window(n, i);
        if (!w) continue;
        double ix = x + i * (icon_px + gap);
        if (e->hits) {
            struct bar_hit h = {ix, ix + icon_px,
                               BAR_HIT_TASKBAR, (void *)(intptr_t)i};
            if (e->hits->n < BAR_MAX_HITS) e->hits->h[e->hits->n++] = h;
        }
        /* focused windows get a small underline, urgent ones a warning dot */
        if (w->focused) {
            set_rgba(e->cr, e->th->primary, 0.95);
            cairo_rectangle(e->cr, ix + 2, e->h - 4, icon_px - 4, 2);
            cairo_fill(e->cr);
        }
        if (w->urgent) {
            set_rgba(e->cr, e->th->error, 1.0);
            cairo_arc(e->cr, ix + icon_px - 3, 4, 2.5, 0, 2 * G_PI);
            cairo_fill(e->cr);
        }
        char *ip = icon_resolve_path(w->app_id);
        cairo_surface_t *icon = ip ? icon_load_surface(ip, (int)icon_px) : (w->app_id[0] ? icon_load_surface(w->app_id, (int)icon_px) : NULL);
        free(ip);
        if (icon) {
            cairo_set_source_surface(e->cr, icon, ix, (e->h - icon_px) / 2.0);
            cairo_paint(e->cr);
            cairo_surface_destroy(icon);
        } else {
            /* no icon: fall back to a truncated title */
            char buf[8];
            snprintf(buf, sizeof(buf), "%.1s", w->title);
            set_rgba(e->cr, e->th->foreground, w->focused ? 1.0 : 0.7);
            cairo_move_to(e->cr, ix + 6, (e->h - 12) / 2.0);
            text_draw(e, e->text, buf, ix + 6);
        }
    }
    return total;
}

static double w_launcher(struct draw_env *e, double x, int draw)
{
    bool vis = e->ctx->launcher && launcher_is_visible(e->ctx->launcher);
    const char *txt = vis ? "v" : ">";
    double tw = text_width(e->text, txt);
    if (draw) {
        set_rgba(e->cr, e->th->foreground, 0.95);
        text_draw(e, e->text, txt, x);
        add_hit(e, x - 8, x + tw + 8, BAR_HIT_LAUNCHER, NULL);
    }
    return tw;
}

static double w_volume(struct draw_env *e, double x, int draw)
{
    const char *txt = "VOL";
    double tw = text_width(e->small, txt);
    if (draw) {
        set_rgba(e->cr, e->th->on_surface_variant, 1.0);
        text_draw(e, e->small, txt, x);
    }
    return tw;
}

static double w_network(struct draw_env *e, double x, int draw)
{
    const char *txt = "NET";
    double tw = text_width(e->small, txt);
    if (draw) {
        set_rgba(e->cr, e->th->on_surface_variant, 1.0);
        text_draw(e, e->small, txt, x);
    }
    return tw;
}

static double w_tray(struct draw_env *e, double x, int draw)
{
    (void)draw;
    return 0; /* not implemented */
}

static double w_kbd(struct draw_env *e, double x, int draw)
{
    const char *txt = "US";
    double tw = text_width(e->small, txt);
    if (draw) {
        set_rgba(e->cr, e->th->on_surface_variant, 1.0);
        text_draw(e, e->small, txt, x);
        add_hit(e, x - 2, x + tw + 2, BAR_HIT_KBD, NULL);
    }
    return tw;
}
/* "LABEL value": label dimmed like Noctalia's on_surface_variant icons */
static double w_metric(struct draw_env *e, double x, int draw, const char *label, int pct, const char *suffix)
{
    if (pct < 0) return 0;
    char val[32];
    snprintf(val, sizeof(val), "%d%%%s", pct, suffix ? suffix : "");
    double lw = text_width(e->small, label);
    double gap = 5;
    double vw = text_width(e->text, val);
    if (draw) {
        set_rgba(e->cr, e->th->on_surface_variant, 1.0);
        text_draw(e, e->small, label, x);
        set_rgba(e->cr, e->th->foreground, 1.0);
        text_draw(e, e->text, val, x + lw + gap);
    }
    return lw + gap + vw;
}

static double widget(struct draw_env *e, const char *name, double x, int draw)
{
    const struct config *c = e->ctx->cfg;
    const struct sysinfo_state *s = e->ctx->sys;
    if (strcmp(name, "launcher") == 0) return w_launcher(e, x, draw);
    if (strcmp(name, "workspaces") == 0) return w_workspaces(e, x, draw);
    if (strcmp(name, "clock") == 0) return w_time(e, x, draw, c->bar.clock_format, 1.0, BAR_HIT_CLOCK);
    if (strcmp(name, "date") == 0) return w_time(e, x, draw, c->bar.date_format, 0.8, BAR_HIT_DATE);
    if (!s) return 0;
    if (strcmp(name, "cpu") == 0) return w_metric(e, x, draw, "CPU", s->cpu_pct, NULL);
    if (strcmp(name, "ram") == 0) return w_metric(e, x, draw, "RAM", s->ram_pct, NULL);
    if (strcmp(name, "battery") == 0)
        return w_metric(e, x, draw, "BAT", s->bat_pct, s->bat_charging ? "+" : NULL);
    if (strcmp(name, "volume") == 0) return w_volume(e, x, draw);
    if (strcmp(name, "network") == 0) return w_network(e, x, draw);
    if (strcmp(name, "tray") == 0) return w_tray(e, x, draw);
    if (strcmp(name, "kbd") == 0 || strcmp(name, "keyboard") == 0 || strcmp(name, "keyboard_layout") == 0) return w_kbd(e, x, draw);
    if (strcmp(name, "taskbar") == 0) return w_taskbar(e, x, draw);
    return 0; /* unknown module: ignored */
}

/* Lay out a section starting at x; returns total width. */
static double section(struct draw_env *e, enum bar_section sec, double x, int draw)
{
    const struct bar_config *b = &e->ctx->cfg->bar;
    double cx = x;
    bool first = true;
    for (int i = 0; i < b->n_modules[sec]; i++) {
        double probe = widget(e, b->modules[sec][i], 0, 0);
        if (probe <= 0) continue; /* hidden widgets take no spacing */
        if (!first) cx += b->spacing;
        widget(e, b->modules[sec][i], cx, draw);
        cx += probe;
        first = false;
    }
    return cx - x;
}

static PangoLayout *make_layout(cairo_t *cr, const char *family, double size, PangoWeight weight)
{
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, family);
    pango_font_description_set_size(fd, (int)(size * PANGO_SCALE));
    pango_font_description_set_weight(fd, weight);
    pango_layout_set_font_description(l, fd);
    pango_font_description_free(fd);
    return l;
}

void bar_draw(cairo_t *cr, int w, int h, const struct bar_ctx *ctx, struct bar_hits *hits)
{
    struct config def;
    struct bar_ctx local = *ctx;
    if (!local.cfg) {
        config_defaults(&def);
        local.cfg = &def;
    }
    const struct config *cfg = local.cfg;
    if (hits) hits->n = 0;

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    set_rgba(cr, cfg->theme.background, 1.0);
    cairo_paint(cr);
    cairo_restore(cr);

    const char *family = cfg->font.family[0] ? cfg->font.family : "sans-serif";
    double size = cfg->font.size > 0 ? cfg->font.size : 11;
    struct draw_env e = {
        .cr = cr,
        .text = make_layout(cr, family, size, PANGO_WEIGHT_MEDIUM), /* Noctalia: weight 500 */
        .small = make_layout(cr, family, size * 0.8, PANGO_WEIGHT_BOLD),
        .ctx = &local,
        .th = &cfg->theme,
        .h = h,
        .hits = NULL,
    };

    double pad = cfg->bar.padding;
    double sw = section(&e, BAR_START, 0, 0);
    double cw = section(&e, BAR_CENTER, 0, 0);
    double ew = section(&e, BAR_END, 0, 0);

    double sx = pad;
    double ex = w - pad - ew;
    double cx = (w - cw) / 2.0;
    if (cx < sx + sw + cfg->bar.spacing) cx = sx + sw + cfg->bar.spacing;

    e.hits = hits;
    section(&e, BAR_START, sx, 1);
    section(&e, BAR_CENTER, cx, 1);
    if (ex >= cx + cw + cfg->bar.spacing) section(&e, BAR_END, ex, 1); /* drop when it would overlap */

    g_object_unref(e.text);
    g_object_unref(e.small);
}

const struct bar_hit *bar_hit_at(const struct bar_hits *hits, double x)
{
    if (!hits) return NULL;
    for (int i = 0; i < hits->n; i++) {
        if (x >= hits->h[i].x0 && x < hits->h[i].x1) return &hits->h[i];
    }
    return NULL;
}
