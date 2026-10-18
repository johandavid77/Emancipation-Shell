#define _GNU_SOURCE
#include "widgets/desktop_widgets.h"
#include "core/panel.h"
#include "core/output.h"
#include "core/wayland.h"
#include "config/config.h"
#include "bar/sysinfo.h"
#include "util/log.h"

#include <cairo.h>
#include <pango/pangocairo.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DW_PAD 18.0
#define DW_GAP 14.0
#define DW_RADIUS 16.0
#define DW_CLOCK_H 132.0
#define DW_SYSMON_H 118.0

struct desktop_widgets {
    struct wayland_ctx *ctx;
    struct output *out;
    struct panel panel;
    const struct config *cfg;
    const struct sysinfo_state *sys;
    int width, height;
};

static void dw_round(cairo_t *cr, double x, double y, double w, double h, double r)
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

static PangoLayout *dw_layout(cairo_t *cr, const char *family, double px, bool bold, const char *text)
{
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, family);
    pango_font_description_set_weight(fd, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
    pango_layout_set_font_description(l, fd);
    pango_layout_set_text(l, text, -1);
    pango_font_description_free(fd);
    return l;
}

static void dw_card(cairo_t *cr, const struct theme_config *th, double x, double y, double w, double h)
{
    cairo_set_source_rgba(cr, th->panel_background.r, th->panel_background.g,
                          th->panel_background.b, 0.72);
    dw_round(cr, x, y, w, h, DW_RADIUS);
    cairo_fill(cr);
}

static double dw_draw_clock(struct desktop_widgets *dw, cairo_t *cr, const struct theme_config *th,
                            double x, double y, double w)
{
    double h = DW_CLOCK_H;
    dw_card(cr, th, x, y, w, h);

    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char timebuf[64], datebuf[64];
    strftime(timebuf, sizeof(timebuf), dw->cfg->bar.clock_format, &tm);
    strftime(datebuf, sizeof(datebuf), dw->cfg->bar.date_format, &tm);

    double cx = x + w / 2.0;
    PangoLayout *lt = dw_layout(cr, dw->cfg->font.family, 46, true, timebuf);
    int twin, tht;
    pango_layout_get_pixel_size(lt, &twin, &tht);
    cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 1.0);
    cairo_move_to(cr, cx - twin / 2.0, y + 24);
    pango_cairo_show_layout(cr, lt);
    g_object_unref(lt);

    PangoLayout *ld = dw_layout(cr, dw->cfg->font.family, 15, false, datebuf);
    int dwin, dht;
    pango_layout_get_pixel_size(ld, &dwin, &dht);
    cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                          th->on_surface_variant.b, 1.0);
    cairo_move_to(cr, cx - dwin / 2.0, y + 24 + tht + 8);
    pango_cairo_show_layout(cr, ld);
    g_object_unref(ld);
    return h;
}

static void dw_metric(cairo_t *cr, struct desktop_widgets *dw, const struct theme_config *th,
                      double x, double y, double w, const char *label, int pct)
{
    PangoLayout *ll = dw_layout(cr, dw->cfg->font.family, 13, false, label);
    int lw, lh;
    pango_layout_get_pixel_size(ll, &lw, &lh);
    cairo_set_source_rgba(cr, th->on_surface_variant.r, th->on_surface_variant.g,
                          th->on_surface_variant.b, 1.0);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, ll);
    g_object_unref(ll);

    char pbuf[16];
    if (pct < 0) snprintf(pbuf, sizeof(pbuf), "--");
    else snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
    PangoLayout *lp = dw_layout(cr, dw->cfg->font.family, 13, true, pbuf);
    int pw, ph;
    pango_layout_get_pixel_size(lp, &pw, &ph);
    cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 1.0);
    cairo_move_to(cr, x + w - pw, y);
    pango_cairo_show_layout(cr, lp);
    g_object_unref(lp);

    double by = y + lh + 6;
    double bh = 8;
    cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g, th->surface_variant.b, 0.9);
    dw_round(cr, x, by, w, bh, bh / 2);
    cairo_fill(cr);
    if (pct >= 0) {
        double fill = w * (pct > 100 ? 100 : pct) / 100.0;
        if (fill < bh) fill = bh;
        cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 1.0);
        dw_round(cr, x, by, fill, bh, bh / 2);
        cairo_fill(cr);
    }
}

static double dw_draw_sysmon(struct desktop_widgets *dw, cairo_t *cr, const struct theme_config *th,
                             double x, double y, double w)
{
    double h = DW_SYSMON_H;
    dw_card(cr, th, x, y, w, h);
    int cpu = dw->sys ? dw->sys->cpu_pct : -1;
    int ram = dw->sys ? dw->sys->ram_pct : -1;
    double iw = w - 2 * DW_PAD;
    dw_metric(cr, dw, th, x + DW_PAD, y + DW_PAD, iw, "CPU", cpu);
    dw_metric(cr, dw, th, x + DW_PAD, y + DW_PAD + 44, iw, "RAM", ram);
    return h;
}

static void dw_draw_cb(struct panel *p, cairo_t *cr, int w, int h)
{
    struct desktop_widgets *dw = p->userdata;
    struct config def;
    const struct theme_config *th = dw->cfg ? &dw->cfg->theme : (config_defaults(&def), &def.theme);
    (void)h;

    double y = DW_PAD;
    if (dw->cfg->widgets.clock) {
        y += dw_draw_clock(dw, cr, th, DW_PAD, y, w - 2 * DW_PAD);
        y += DW_GAP;
    }
    if (dw->cfg->widgets.sysmon) {
        y += dw_draw_sysmon(dw, cr, th, DW_PAD, y, w - 2 * DW_PAD);
        y += DW_GAP;
    }
}

static int dw_total_height(const struct config *cfg)
{
    double h = DW_PAD * 2;
    int cards = 0;
    if (cfg->widgets.clock) { h += DW_CLOCK_H; cards++; }
    if (cfg->widgets.sysmon) { h += DW_SYSMON_H; cards++; }
    if (cards > 1) h += DW_GAP * (cards - 1);
    return (int)h;
}

static void dw_relayout(struct desktop_widgets *dw)
{
    if (!dw->cfg->widgets.enabled) return;
    int w = dw->cfg->widgets.width;
    int h = dw_total_height(dw->cfg);
    if (w == dw->width && h == dw->height && panel_is_visible(&dw->panel)) {
        panel_mark_dirty(&dw->panel);
        return;
    }
    if (panel_is_visible(&dw->panel)) panel_hide(&dw->panel);
    dw->width = w;
    dw->height = h;
    panel_show_at(&dw->panel, dw->out, w, h,
                  dw->cfg->widgets.position == 1 ? PANEL_ANCHOR_TOP_RIGHT : PANEL_ANCHOR_TOP_LEFT);
}

struct desktop_widgets *desktop_widgets_create(struct wayland_ctx *ctx, struct output *out,
                                               const struct config *cfg)
{
    if (!ctx || !out || !cfg) return NULL;
    struct desktop_widgets *dw = calloc(1, sizeof(*dw));
    if (!dw) return NULL;
    dw->ctx = ctx;
    dw->out = out;
    dw->cfg = cfg;
    panel_init(&dw->panel, ctx);
    panel_set_callbacks(&dw->panel, dw_draw_cb, NULL, NULL, NULL, dw);
    panel_set_persistent(&dw->panel, true);
    panel_set_layer(&dw->panel, PANEL_LAYER_BOTTOM);
    if (cfg->widgets.enabled) dw_relayout(dw);
    log_debug("desktop widgets created");
    return dw;
}

void desktop_widgets_destroy(struct desktop_widgets *dw)
{
    if (!dw) return;
    panel_fini(&dw->panel);
    free(dw);
}

void desktop_widgets_update_config(struct desktop_widgets *dw, const struct config *cfg)
{
    if (!dw || !cfg) return;
    dw->cfg = cfg;
    if (cfg->widgets.enabled) {
        dw_relayout(dw);
    } else if (panel_is_visible(&dw->panel)) {
        panel_hide(&dw->panel);
    }
}

struct panel *desktop_widgets_surface(struct desktop_widgets *dw)
{
    return dw ? &dw->panel : NULL;
}

void desktop_widgets_mark_dirty(struct desktop_widgets *dw)
{
    if (!dw || !dw->cfg->widgets.enabled) return;
    panel_mark_dirty(&dw->panel);
}

void desktop_widgets_set_sysinfo(struct desktop_widgets *dw, const struct sysinfo_state *sys)
{
    if (dw) dw->sys = sys;
}

bool desktop_widgets_is_visible(struct desktop_widgets *dw)
{
    return dw && panel_is_visible(&dw->panel);
}
