#include "bar/bar.h"
#include "bar/workspaces.h"
#include "config/config.h"
#include <pango/pangocairo.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PAD 10.0
#define PILL_PAD 8.0
#define PILL_GAP 4.0

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

/* Returns logical text width; draws only when `draw` is set. */
static double text_at(cairo_t *cr, PangoLayout *layout, const char *txt,
                      double x, int bar_h, int draw)
{
    int tw, th;
    pango_layout_set_text(layout, txt, -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    if (draw) {
        cairo_move_to(cr, x, (bar_h - th) / 2.0);
        pango_cairo_show_layout(cr, layout);
    }
    return tw;
}

void bar_draw(cairo_t *cr, int w, int h, const struct config *cfg,
              struct workspace_manager *wm, struct wl_output *output)
{
    struct config def;
    if (!cfg) {
        config_defaults(&def);
        cfg = &def;
    }
    const struct color_rgba bg = cfg->theme.background;
    const struct color_rgba fg = cfg->theme.foreground;

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    set_rgba(cr, bg, 1.0);
    cairo_paint(cr);
    cairo_restore(cr);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    char fontdesc[300];
    snprintf(fontdesc, sizeof(fontdesc), "%s %d",
             cfg->font.family[0] ? cfg->font.family : "Sans",
             cfg->font.size > 0 ? cfg->font.size : 12);
    PangoFontDescription *fd = pango_font_description_from_string(fontdesc);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);

    /* left: workspaces */
    struct workspace_info ws[32];
    size_t n = workspaces_snapshot(wm, output, ws, 32);
    double x = PAD;
    double pill_h = h - 8.0;
    if (pill_h < 4) pill_h = h;
    for (size_t i = 0; i < n; i++) {
        char label[64];
        if (ws[i].name[0]) snprintf(label, sizeof(label), "%s", ws[i].name);
        else snprintf(label, sizeof(label), "%zu", i + 1);
        double tw = text_at(cr, layout, label, 0, h, 0);
        double pw = tw + 2 * PILL_PAD;
        if (pw < pill_h) pw = pill_h;
        if (ws[i].active) {
            set_rgba(cr, fg, 1.0);
            rounded_rect(cr, x, (h - pill_h) / 2.0, pw, pill_h, 6);
            cairo_fill(cr);
            set_rgba(cr, bg, 1.0);
        } else if (ws[i].urgent) {
            cairo_set_source_rgba(cr, 0.85, 0.30, 0.30, 1.0);
            rounded_rect(cr, x, (h - pill_h) / 2.0, pw, pill_h, 6);
            cairo_fill(cr);
            set_rgba(cr, fg, 1.0);
        } else {
            set_rgba(cr, fg, ws[i].hidden ? 0.45 : 0.75);
        }
        text_at(cr, layout, label, x + (pw - tw) / 2.0, h, 1);
        x += pw + PILL_GAP;
    }

    /* center: clock, right: date */
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char clock_s[32], date_s[64];
    strftime(clock_s, sizeof(clock_s), "%H:%M", &tmv);
    strftime(date_s, sizeof(date_s), "%a %d %b", &tmv);

    set_rgba(cr, fg, 1.0);
    double cw = text_at(cr, layout, clock_s, 0, h, 0);
    double cx = (w - cw) / 2.0;
    if (cx < x) cx = x; /* never overlap the workspace list */
    text_at(cr, layout, clock_s, cx, h, 1);

    set_rgba(cr, fg, 0.75);
    double dw = text_at(cr, layout, date_s, 0, h, 0);
    double dx = w - PAD - dw;
    if (dx > cx + cw + PAD) text_at(cr, layout, date_s, dx, h, 1);

    g_object_unref(layout);
}
