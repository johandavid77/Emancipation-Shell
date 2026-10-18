#include "switcher/switcher.h"
#include "core/wayland.h"
#include "core/output.h"
#include "config/config.h"
#include "compositors/niri_ipc.h"
#include "util/log.h"

#include <cairo.h>
#include <pango/pangocairo.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define SW_W 880
#define SW_H 520
#define SW_GAP 14.0
#define SW_COLS 3
#define SW_CARD_H 104.0
#define SW_PAD 22.0
#define SW_TITLE_H 52.0
#define SW_ICON 60.0
#define SW_MAX_CARDS 12

struct sw_card {
    double x, y, w, h;
};

struct switcher {
    struct panel panel;
    struct niri_ipc *niri;
    const struct config *cfg;
    struct output *fallback_out;
    int sel;
    struct sw_card cards[SW_MAX_CARDS];
    int n_cards;
};

static void sw_round(cairo_t *cr, double x, double y, double w, double h, double r)
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

static void sw_letter(const char *s, char *out, size_t n)
{
    if (!s || !*s) { snprintf(out, n, "?"); return; }
    while (*s == ' ') s++;
    unsigned char c = (unsigned char)s[0];
    if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
    snprintf(out, n, "%c", c);
}

static void sw_text(cairo_t *cr, const char *font, const char *txt,
                    double x, double y, double r, double g, double b, bool center,
                    double max_w)
{
    PangoLayout *l = pango_cairo_create_layout(cr);
    PangoFontDescription *fd = pango_font_description_from_string(font);
    pango_layout_set_font_description(l, fd);
    pango_layout_set_text(l, txt, -1);
    pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
    if (max_w > 0) pango_layout_set_width(l, (int)(max_w * PANGO_SCALE));
    int tw, th;
    pango_layout_get_pixel_size(l, &tw, &th);
    cairo_set_source_rgba(cr, r, g, b, 1.0);
    double px = center ? x - tw / 2.0 : x;
    double py = center ? y - th / 2.0 : y;
    cairo_move_to(cr, px, py);
    pango_cairo_show_layout(cr, l);
    pango_font_description_free(fd);
    g_object_unref(l);
}

static const struct config *sw_cfg(struct switcher *sw)
{
    static struct config def;
    if (sw->cfg) return sw->cfg;
    config_defaults(&def);
    return &def;
}

/* Max cards that fit in the overlay (used to bound navigation). */
static int sw_capacity(void)
{
    double top = SW_TITLE_H + SW_PAD;
    double avail = SW_H - top - SW_PAD;
    int rows = (int)((avail + SW_GAP) / (SW_CARD_H + SW_GAP));
    if (rows < 1) rows = 1;
    return rows * SW_COLS;
}

static int sw_count(struct switcher *sw)
{
    if (!sw->niri) return 0;
    int n = niri_ipc_window_count(sw->niri);
    int cap = sw_capacity();
    if (n > cap) n = cap;
    if (n > SW_MAX_CARDS) n = SW_MAX_CARDS;
    return n;
}

static void sw_draw(struct panel *p, cairo_t *cr, int w, int h)
{
    struct switcher *sw = p->userdata;
    const struct config *cfg = sw_cfg(sw);
    const struct theme_config *th = &cfg->theme;
    int n = sw_count(sw);

    cairo_set_source_rgba(cr, th->panel_background.r, th->panel_background.g,
                          th->panel_background.b, 0.97);
    sw_round(cr, 0, 0, w, h, 18.0);
    cairo_fill(cr);

    /* title + hint */
    sw_text(cr, "sans-serif Bold 16", "Cambiar de ventana",
            SW_PAD, SW_PAD - 4, th->foreground.r, th->foreground.g, th->foreground.b,
            false, 0);
    sw_text(cr, "sans-serif 12", "Tab/\xE2\x86\x91\xE2\x86\x93 moverse \xC2\xB7 Enter elegir \xC2\xB7 Esc cerrar",
            w - SW_PAD, SW_PAD - 2, th->on_surface_variant.r, th->on_surface_variant.g,
            th->on_surface_variant.b, false, 0);

    double usable = w - 2 * SW_PAD;
    double card_w = (usable - (SW_COLS - 1) * SW_GAP) / SW_COLS;
    double y0 = SW_TITLE_H + SW_PAD;

    sw->n_cards = n;
    for (int i = 0; i < n; i++) {
        int col = i % SW_COLS;
        int row = i / SW_COLS;
        double x = SW_PAD + col * (card_w + SW_GAP);
        double y = y0 + row * (SW_CARD_H + SW_GAP);
        sw->cards[i].x = x;
        sw->cards[i].y = y;
        sw->cards[i].w = card_w;
        sw->cards[i].h = SW_CARD_H;

        const struct niri_window *win = niri_ipc_window(sw->niri, i);
        bool selected = (i == sw->sel);
        bool focused = win ? win->focused : false;

        struct color_rgba bg = th->surface_variant;
        if (selected) {
            bg.r = bg.r * 0.5f + th->primary.r * 0.5f;
            bg.g = bg.g * 0.5f + th->primary.g * 0.5f;
            bg.b = bg.b * 0.5f + th->primary.b * 0.5f;
        }
        cairo_set_source_rgba(cr, bg.r, bg.g, bg.b, selected ? 1.0 : 0.9);
        sw_round(cr, x, y, card_w, SW_CARD_H, 14.0);
        cairo_fill(cr);
        if (selected) {
            cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 1.0);
            cairo_set_line_width(cr, 2.5);
            sw_round(cr, x + 1.25, y + 1.25, card_w - 2.5, SW_CARD_H - 2.5, 12.5);
            cairo_stroke(cr);
        }

        /* letter tile */
        double ix = x + 14, iy = y + (SW_CARD_H - SW_ICON) / 2.0;
        cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b,
                              selected ? 1.0 : 0.82);
        sw_round(cr, ix, iy, SW_ICON, SW_ICON, SW_ICON * 0.28);
        cairo_fill(cr);
        char letter[8];
        sw_letter(win && win->app_id[0] ? win->app_id : (win ? win->title : ""),
                  letter, sizeof(letter));
        char lf[32];
        snprintf(lf, sizeof(lf), "sans-serif Bold %.0f", SW_ICON * 0.5);
        sw_text(cr, lf, letter, ix + SW_ICON / 2.0, iy + SW_ICON / 2.0,
                th->on_primary.r, th->on_primary.g, th->on_primary.b, true, 0);

        /* title + app id */
        double tx = ix + SW_ICON + 12;
        double tw = x + card_w - tx - 12;
        const char *title = (win && win->title[0]) ? win->title : "(sin titulo)";
        const char *app = (win && win->app_id[0]) ? win->app_id : "?";
        sw_text(cr, "sans-serif Bold 13", title, tx, y + 26,
                th->foreground.r, th->foreground.g, th->foreground.b, false, tw);
        struct color_rgba sub = selected ? th->on_primary : th->on_surface_variant;
        sw_text(cr, "sans-serif 11", app, tx, y + 52, sub.r * 0.9, sub.g * 0.9,
                sub.b * 0.9, false, tw);
        if (focused) {
            sw_text(cr, "sans-serif 10", "activa", tx, y + 74,
                    th->primary.r, th->primary.g, th->primary.b, false, tw);
        }
    }

    if (n == 0) {
        sw_text(cr, "sans-serif 14", "No hay ventanas abiertas", w / 2.0, h / 2.0,
                th->on_surface_variant.r, th->on_surface_variant.g,
                th->on_surface_variant.b, true, 0);
    }
}

static int sw_card_at(struct switcher *sw, double x, double y)
{
    for (int i = 0; i < sw->n_cards; i++) {
        struct sw_card *c = &sw->cards[i];
        if (x >= c->x && x <= c->x + c->w && y >= c->y && y <= c->y + c->h)
            return i;
    }
    return -1;
}

static bool sw_key(struct panel *p, uint32_t keysym, uint32_t mods)
{
    struct switcher *sw = p->userdata;
    bool shift = (mods & 2) != 0;
    int n = sw_count(sw);

    switch (keysym) {
    case XKB_KEY_Tab:
        if (shift) sw->sel = (sw->sel - 1 + n) % (n ? n : 1);
        else sw->sel = (sw->sel + 1) % (n ? n : 1);
        panel_mark_dirty(p);
        return true;
    case XKB_KEY_ISO_Left_Tab:
        sw->sel = (sw->sel - 1 + n) % (n ? n : 1);
        panel_mark_dirty(p);
        return true;
    case XKB_KEY_Right:
    case XKB_KEY_KP_Right:
        if (n) sw->sel = (sw->sel + 1) % n;
        panel_mark_dirty(p);
        return true;
    case XKB_KEY_Left:
    case XKB_KEY_KP_Left:
        if (n) sw->sel = (sw->sel - 1 + n) % n;
        panel_mark_dirty(p);
        return true;
    case XKB_KEY_Down:
    case XKB_KEY_KP_Down:
        if (n) sw->sel = (sw->sel + SW_COLS) % n;
        panel_mark_dirty(p);
        return true;
    case XKB_KEY_Up:
    case XKB_KEY_KP_Up:
        if (n) sw->sel = (sw->sel - SW_COLS + n) % n;
        panel_mark_dirty(p);
        return true;
    case XKB_KEY_Home:
        sw->sel = 0;
        panel_mark_dirty(p);
        return true;
    case XKB_KEY_End:
        sw->sel = n > 0 ? n - 1 : 0;
        panel_mark_dirty(p);
        return true;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        switcher_select(sw);
        return true;
    case XKB_KEY_Escape:
        switcher_hide(sw);
        return true;
    default:
        return false;
    }
}

static bool sw_click(struct panel *p, double x, double y)
{
    struct switcher *sw = p->userdata;
    int idx = sw_card_at(sw, x, y);
    if (idx < 0) return false;
    sw->sel = idx;
    switcher_select(sw);
    return true;
}

static void sw_motion(struct panel *p, double x, double y)
{
    struct switcher *sw = p->userdata;
    int idx = sw_card_at(sw, x, y);
    if (idx >= 0 && idx != sw->sel) {
        sw->sel = idx;
        panel_mark_dirty(p);
    }
}

struct switcher *switcher_create(struct wayland_ctx *ctx, const struct config **cfg,
                                 struct output *out)
{
    struct switcher *sw = calloc(1, sizeof(*sw));
    if (!sw) return NULL;
    panel_init(&sw->panel, ctx);
    sw->cfg = cfg ? *cfg : NULL;
    sw->fallback_out = out;
    sw->sel = 0;
    panel_set_callbacks(&sw->panel, sw_draw, sw_key, sw_click, sw_motion, sw);
    return sw;
}

void switcher_destroy(struct switcher *sw)
{
    if (!sw) return;
    panel_fini(&sw->panel);
    free(sw);
}

void switcher_set_niri(struct switcher *sw, struct niri_ipc *niri)
{
    if (sw) sw->niri = niri;
}

void switcher_set_config(struct switcher *sw, const struct config *cfg)
{
    if (!sw) return;
    sw->cfg = cfg;
    if (panel_is_visible(&sw->panel)) panel_mark_dirty(&sw->panel);
}

struct panel *switcher_surface(struct switcher *sw)
{
    return sw ? &sw->panel : NULL;
}

void switcher_show(struct switcher *sw, struct output *out)
{
    if (!sw) return;
    if (!out) out = sw->fallback_out;
    int n = sw_count(sw);
    sw->sel = 0;
    for (int i = 0; i < n; i++) {
        const struct niri_window *win = niri_ipc_window(sw->niri, i);
        if (win && win->focused) { sw->sel = i; break; }
    }
    if (!panel_show(&sw->panel, out, SW_W, SW_H)) {
        log_warn("switcher: show failed");
        return;
    }
    log_info("switcher shown (%d windows)", n);
}

void switcher_hide(struct switcher *sw)
{
    if (!sw) return;
    if (panel_is_visible(&sw->panel)) {
        panel_hide(&sw->panel);
        log_info("switcher hidden");
    }
}

void switcher_toggle(struct switcher *sw, struct output *out)
{
    if (!sw) return;
    if (panel_is_visible(&sw->panel)) switcher_hide(sw);
    else switcher_show(sw, out);
}

bool switcher_visible(struct switcher *sw)
{
    return sw && panel_is_visible(&sw->panel);
}

void switcher_next(struct switcher *sw)
{
    if (!sw) return;
    int n = sw_count(sw);
    if (n) sw->sel = (sw->sel + 1) % n;
    panel_mark_dirty(&sw->panel);
}

void switcher_prev(struct switcher *sw)
{
    if (!sw) return;
    int n = sw_count(sw);
    if (n) sw->sel = (sw->sel - 1 + n) % n;
    panel_mark_dirty(&sw->panel);
}

void switcher_select(struct switcher *sw)
{
    if (!sw) return;
    int n = sw_count(sw);
    if (sw->sel >= 0 && sw->sel < n && sw->niri) {
        const struct niri_window *win = niri_ipc_window(sw->niri, sw->sel);
        if (win) {
            niri_ipc_focus(sw->niri, win->id);
            log_info("switcher: focus window %llu", (unsigned long long)win->id);
        }
    }
    switcher_hide(sw);
}

void switcher_refresh(struct switcher *sw)
{
    if (!sw || !sw->cfg) return;
    int n = sw_count(sw);
    if (sw->sel >= n) sw->sel = n > 0 ? n - 1 : 0;
    if (panel_is_visible(&sw->panel)) panel_mark_dirty(&sw->panel);
}
