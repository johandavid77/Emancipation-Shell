#include "control/calendar_panel.h"
#include "core/output.h"
#include "core/wayland.h"
#include "config/config.h"
#include "util/log.h"

#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Noctalia maps a clock click to "panel-toggle control-center calendar"
 * (shell/bar/widget_gesture_defaults.cpp). The control center does not exist
 * in this port yet, so the calendar lives in its own panel surface. */

#define CP_W 340
#define CP_H 400
#define CP_PAD 18.0
#define CP_HEAD_H 34.0
#define CP_WD_H 20.0
#define CP_DAY_H 38.0

struct calendar_panel {
    struct panel panel;
    const struct config *cfg;
    struct output *fallback_out;
    struct tm view;    /* month being displayed */
    int selected;      /* 1..31 */
    PangoLayout *layout;
    PangoLayout *small;
    PangoLayout *title;
    /* geometry for pointer hit testing */
    double grid_x0, grid_y0, cell_w, cell_h;
};

static void cp_sync_today(struct calendar_panel *cp)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    cp->view = t;
    cp->view.tm_mday = 1;
    cp->view.tm_hour = 0;
    cp->view.tm_min = 0;
    cp->view.tm_sec = 0;
    cp->selected = t.tm_mday;
}

static int cp_days_in_month(int year, int mon /* 0-11 */)
{
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mon == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return d[mon];
}

/* 0 = Sunday */
static int cp_first_weekday(int year, int mon)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = mon;
    t.tm_mday = 1;
    time_t when = mktime(&t);
    struct tm n;
    localtime_r(&when, &n);
    return n.tm_wday;
}

static void round_rect(cairo_t *cr, double x, double y, double w, double h, double r)
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

static bool cp_is_today(const struct calendar_panel *cp, int day)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_year == cp->view.tm_year && t.tm_mon == cp->view.tm_mon && t.tm_mday == day;
}

static void cp_draw(struct panel *p, cairo_t *cr, int w, int h)
{
    struct calendar_panel *cp = p->userdata;
    struct config def;
    config_defaults(&def);
    const struct theme_config *th = cp->cfg ? &cp->cfg->theme : &def.theme;

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, th->background.r, th->background.g, th->background.b, 1.0);
    cairo_paint(cr);
    cairo_restore(cr);

    if (!cp->layout) {
        cp->layout = pango_cairo_create_layout(cr);
        cp->small = pango_cairo_create_layout(cr);
        cp->title = pango_cairo_create_layout(cr);
    }
    const char *family = (cp->cfg && cp->cfg->font.family[0]) ? cp->cfg->font.family : "sans-serif";
    double fsize = (cp->cfg && cp->cfg->font.size > 0) ? cp->cfg->font.size : 13.0;
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, family);
    pango_font_description_set_size(fd, (int)(fsize * PANGO_SCALE));
    pango_layout_set_font_description(cp->layout, fd);
    pango_font_description_set_size(fd, (int)((fsize - 2) * PANGO_SCALE));
    pango_layout_set_font_description(cp->small, fd);
    pango_font_description_set_size(fd, (int)((fsize + 3) * PANGO_SCALE));
    pango_font_description_set_weight(fd, PANGO_WEIGHT_MEDIUM);
    pango_layout_set_font_description(cp->title, fd);
    pango_font_description_free(fd);

    /* ---- header: month/year + arrows ---- */
    char buf[64];
    strftime(buf, sizeof(buf), "%B %Y", &cp->view);
    pango_layout_set_text(cp->title, buf, -1);
    cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 1.0);
    cairo_move_to(cr, CP_PAD, CP_PAD);
    pango_cairo_show_layout(cr, cp->title);

    cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.7);
    pango_layout_set_text(cp->small, "<", -1);
    cairo_move_to(cr, w - CP_PAD - 46, CP_PAD + 6);
    pango_cairo_show_layout(cr, cp->small);
    pango_layout_set_text(cp->small, ">", -1);
    cairo_move_to(cr, w - CP_PAD - 18, CP_PAD + 6);
    pango_cairo_show_layout(cr, cp->small);

    /* ---- weekday header ---- */
    static const char *wd[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    double grid_w = w - 2 * CP_PAD;
    cp->cell_w = grid_w / 7.0;
    cp->cell_h = CP_DAY_H;
    cp->grid_x0 = CP_PAD;
    cp->grid_y0 = CP_PAD + CP_HEAD_H + CP_WD_H;

    for (int i = 0; i < 7; i++) {
        pango_layout_set_text(cp->small, wd[i], -1);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.5);
        cairo_move_to(cr, cp->grid_x0 + i * cp->cell_w + cp->cell_w / 2 - 8,
                      CP_PAD + CP_HEAD_H + 4);
        pango_cairo_show_layout(cr, cp->small);
    }

    /* ---- day grid ---- */
    int dim = cp_first_weekday(cp->view.tm_year + 1900, cp->view.tm_mon);
    int ndays = cp_days_in_month(cp->view.tm_year + 1900, cp->view.tm_mon);
    int prev_dim = cp_days_in_month(cp->view.tm_year + 1900, (cp->view.tm_mon + 11) % 12);
    for (int cell = 0; cell < 42; cell++) {
        int col = cell % 7;
        int row = cell / 7;
        int n = cell - dim + 1;
        int day;
        bool outside = false;
        if (n < 1) { day = prev_dim + n; outside = true; }
        else if (n > ndays) { day = n - ndays; outside = true; }
        else day = n;
        double x = cp->grid_x0 + col * cp->cell_w;
        double y = cp->grid_y0 + row * cp->cell_h;
        if (y + cp->cell_h > h - CP_PAD) break;

        bool today = !outside && cp_is_today(cp, day);
        bool sel = !outside && day == cp->selected;
        if (sel) {
            cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 0.30);
            round_rect(cr, x + 3, y + 2, cp->cell_w - 6, cp->cell_h - 6, 6);
            cairo_fill(cr);
        }
        char dbuf[8];
        snprintf(dbuf, sizeof(dbuf), "%d", day);
        pango_layout_set_text(cp->layout, dbuf, -1);
        double alpha = outside ? 0.3 : (sel ? 1.0 : 0.85);
        if (today) {
            cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 1.0);
        } else {
            cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, alpha);
        }
        cairo_move_to(cr, x + cp->cell_w / 2 - 7, y + cp->cell_h / 2 - 8);
        pango_cairo_show_layout(cr, cp->layout);
    }

    /* ---- agenda for the selected day ---- */
    double ay = cp->grid_y0 + 6 * cp->cell_h + 8.0;
    if (ay < h - CP_PAD) {
        char abuf[96];
        snprintf(abuf, sizeof(abuf), "%d %B %d", cp->selected, cp->view.tm_mon + 1,
                 cp->view.tm_year + 1900);
        pango_layout_set_text(cp->small, abuf, -1);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.7);
        cairo_move_to(cr, CP_PAD, ay);
        pango_cairo_show_layout(cr, cp->small);

        pango_layout_set_text(cp->small, "no events", -1);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.4);
        cairo_move_to(cr, CP_PAD, ay + 20);
        pango_cairo_show_layout(cr, cp->small);
    }
}

static void cp_add_month(struct calendar_panel *cp, int delta)
{
    int m = cp->view.tm_mon + delta;
    int y = cp->view.tm_year;
    while (m < 0) { m += 12; y--; }
    while (m > 11) { m -= 12; y++; }
    cp->view.tm_mon = m;
    cp->view.tm_year = y;
    int ndays = cp_days_in_month(y + 1900, m);
    if (cp->selected > ndays) cp->selected = ndays;
}

static void cp_sync_selected_after_nav(struct calendar_panel *cp)
{
    (void)cp;
}

static bool cp_key(struct panel *p, uint32_t keysym, uint32_t mods)
{
    (void)mods;
    struct calendar_panel *cp = p->userdata;
    int ndays;
    switch (keysym) {
        case 0xff1b: /* Escape */
            panel_hide(p);
            return true;
        case 0xff51: /* Left: previous month */
            cp_add_month(cp, -1);
            cp_sync_selected_after_nav(cp);
            panel_mark_dirty(p);
            return true;
        case 0xff53: /* Right: next month */
            cp_add_month(cp, +1);
            cp_sync_selected_after_nav(cp);
            panel_mark_dirty(p);
            return true;
        case 0xff52: /* Up: previous week */
            cp->selected -= 7;
            ndays = cp_days_in_month(cp->view.tm_year + 1900, cp->view.tm_mon);
            if (cp->selected < 1) cp->selected = 1;
            if (cp->selected > ndays) cp->selected = ndays;
            panel_mark_dirty(p);
            return true;
        case 0xff54: /* Down: next week */
            ndays = cp_days_in_month(cp->view.tm_year + 1900, cp->view.tm_mon);
            cp->selected += 7;
            if (cp->selected > ndays) cp->selected = ndays;
            panel_mark_dirty(p);
            return true;
        default:
            return false;
    }
}

static bool cp_click(struct panel *p, double x, double y)
{
    struct calendar_panel *cp = p->userdata;
    int w = cp->panel.logical_w;
    /* header arrows */
    if (y < CP_PAD + CP_HEAD_H) {
        if (x > w - CP_PAD - 60 && x < w - CP_PAD - 34) {
            cp_add_month(cp, -1);
            panel_mark_dirty(p);
            return true;
        }
        if (x > w - CP_PAD - 26) {
            cp_add_month(cp, +1);
            panel_mark_dirty(p);
            return true;
        }
        return true;
    }
    if (y < cp->grid_y0) return true;
    int col = (int)((x - cp->grid_x0) / cp->cell_w);
    int row = (int)((y - cp->grid_y0) / cp->cell_h);
    if (col < 0 || col > 6 || row < 0 || row > 5) return true;
    int cell = row * 7 + col;
    int dim = cp_first_weekday(cp->view.tm_year + 1900, cp->view.tm_mon);
    int ndays = cp_days_in_month(cp->view.tm_year + 1900, cp->view.tm_mon);
    int n = cell - dim + 1;
    if (n < 1) {
        cp_add_month(cp, -1);
        n = cell - cp_first_weekday(cp->view.tm_year + 1900, cp->view.tm_mon) + 1;
    } else if (n > ndays) {
        cp_add_month(cp, +1);
        n = cell - cp_first_weekday(cp->view.tm_year + 1900, cp->view.tm_mon) + 1;
    }
    if (n >= 1 && n <= cp_days_in_month(cp->view.tm_year + 1900, cp->view.tm_mon)) {
        cp->selected = n;
    }
    panel_mark_dirty(p);
    return true;
}

static void cp_motion(struct panel *p, double x, double y)
{
    (void)p; (void)x; (void)y; /* no per-cell hover highlight yet */
}

struct calendar_panel *calendar_panel_create(struct wayland_ctx *ctx, const struct config **cfg,
                                             struct output *out)
{
    struct calendar_panel *cp = calloc(1, sizeof(*cp));
    if (!cp) return NULL;
    panel_init(&cp->panel, ctx);
    cp->cfg = cfg ? *cfg : NULL;
    cp->fallback_out = out;
    cp_sync_today(cp);
    panel_set_callbacks(&cp->panel, cp_draw, cp_key, cp_click, cp_motion, cp);
    log_info("calendar panel created");
    return cp;
}

void calendar_panel_destroy(struct calendar_panel *cp)
{
    if (!cp) return;
    panel_fini(&cp->panel);
    free(cp);
}

struct panel *calendar_panel_surface(struct calendar_panel *cp)
{
    return cp ? &cp->panel : NULL;
}

void calendar_panel_toggle_proxy(void *userdata)
{
    struct calendar_panel *cp = userdata;
    if (!cp) return;
    if (panel_is_visible(&cp->panel)) {
        panel_hide(&cp->panel);
        return;
    }
    struct output *out = cp->panel.out ? cp->panel.out : cp->fallback_out;
    cp_sync_today(cp);
    if (!panel_show(&cp->panel, out, CP_W, CP_H)) log_warn("calendar panel: show failed");
}