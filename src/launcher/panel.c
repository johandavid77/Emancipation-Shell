#include "launcher/panel.h"
#include "core/output.h"
#include "core/wayland.h"
#include "config/config.h"
#include "launcher/launcher.h"
#include "util/log.h"

#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>

#define LP_MAX_RESULTS 32
#define LP_W 560
#define LP_H 460
#define LP_PAD 22.0
#define LP_INPUT_H 44.0
#define LP_ROW_H 30.0

struct launcher_panel {
    struct panel panel;
    struct launcher *launcher;
    struct output *fallback_out; /* output used before the panel has a surface */
    const struct config *cfg;
    char query[128];
    int results[LP_MAX_RESULTS];
    int count;
    int selected;
    /* geometry of the row list, kept for pointer hit testing */
    double row_x0, row_w, row_y0;
    PangoLayout *layout;
    PangoLayout *small;
};

static void lp_refresh(struct launcher_panel *lp)
{
    launcher_set_query(lp->launcher, lp->query);
    launcher_get_results(lp->launcher, lp->results, &lp->count, LP_MAX_RESULTS);
    if (lp->count > LP_MAX_RESULTS) lp->count = LP_MAX_RESULTS;
    lp->selected = 0;
}

static void lp_activate(struct launcher_panel *lp, int idx)
{
    const char *name = launcher_get_name(lp->launcher, idx);
    log_info("launcher activate: %s", name ? name : "?");
    launcher_exec_from_idx(lp->launcher, idx);
    launcher_panel_hide(lp);
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

static void lp_draw(struct panel *p, cairo_t *cr, int w, int h)
{
    struct launcher_panel *lp = p->userdata;
    struct config defcfg;
    config_defaults(&defcfg);
    const struct theme_config *th = lp->cfg ? &lp->cfg->theme : &defcfg.theme;

    /* the layer surface is opaque, so paint the background over everything */
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, th->panel_background.r, th->panel_background.g, th->panel_background.b, 1.0);
    cairo_paint(cr);
    cairo_restore(cr);

    const struct config *cfg = lp->cfg;
    const char *family = (cfg && cfg->font.family[0]) ? cfg->font.family : "sans-serif";
    double fsize = (cfg && cfg->font.size > 0) ? cfg->font.size : 13.0;
    if (!lp->layout) {
        lp->layout = pango_cairo_create_layout(cr);
        lp->small = pango_cairo_create_layout(cr);
    }
    PangoFontDescription *fd = pango_font_description_new();
    pango_font_description_set_family(fd, family);
    pango_font_description_set_size(fd, (int)(fsize * PANGO_SCALE));
    pango_layout_set_font_description(lp->layout, fd);
    pango_font_description_set_size(fd, (int)((fsize - 2.0) * PANGO_SCALE));
    pango_layout_set_font_description(lp->small, fd);
    pango_font_description_free(fd);

    /* ---- search input row ---- */
    double iy = LP_PAD;
    cairo_set_source_rgba(cr, th->surface_variant.r, th->surface_variant.g, th->surface_variant.b, 0.25);
    round_rect(cr, LP_PAD, iy, w - 2 * LP_PAD, LP_INPUT_H, 8.0);
    cairo_fill(cr);

    char buf[160];
    snprintf(buf, sizeof(buf), "%s%s", lp->query, "|");
    pango_layout_set_text(lp->layout, buf, -1);
    cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 1.0);
    cairo_move_to(cr, LP_PAD + 14.0, iy + (LP_INPUT_H - 18) / 2.0);
    pango_cairo_show_layout(cr, lp->layout);

    /* ---- result rows ---- */
    lp->row_x0 = LP_PAD + 6.0;
    lp->row_w = w - 2 * LP_PAD - 12.0;
    lp->row_y0 = iy + LP_INPUT_H + 10.0;

    for (int i = 0; i < lp->count; i++) {
        double ry = lp->row_y0 + i * LP_ROW_H;
        if (ry + LP_ROW_H > h - LP_PAD) break;
        int idx = lp->results[i];
        const char *name = launcher_get_name(lp->launcher, idx);
        if (!name) continue;
        if (i == lp->selected) {
            cairo_set_source_rgba(cr, th->primary.r, th->primary.g, th->primary.b, 0.22);
            round_rect(cr, lp->row_x0, ry, lp->row_w, LP_ROW_H - 4.0, 6.0);
            cairo_fill(cr);
        }
        double icon_px = 20.0;
        double row_h = LP_ROW_H - 4.0;
        double text_x = lp->row_x0 + 12.0;
        cairo_surface_t *icon = launcher_icon_for(lp->launcher, idx, (int)icon_px);
        if (icon) {
            cairo_set_source_surface(cr, icon, text_x, ry + (row_h - icon_px) / 2.0);
            cairo_paint(cr);
            text_x += icon_px + 12.0;
        }
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b,
                              i == lp->selected ? 1.0 : 0.85);
        pango_layout_set_text(lp->layout, name, -1);
        cairo_move_to(cr, text_x, ry + (row_h - 16.0) / 2.0);
        pango_cairo_show_layout(cr, lp->layout);
    }

    if (lp->count == 0) {
        const char *msg = "no results";
        pango_layout_set_text(lp->small, msg, -1);
        cairo_set_source_rgba(cr, th->foreground.r, th->foreground.g, th->foreground.b, 0.5);
        cairo_move_to(cr, lp->row_x0 + 12.0, lp->row_y0 + 8.0);
        pango_cairo_show_layout(cr, lp->small);
    }
}

static bool lp_key(struct panel *p, uint32_t keysym, uint32_t modifiers)
{
    (void)modifiers;
    struct launcher_panel *lp = p->userdata;
    switch (keysym) {
        case 0xff1b: /* Escape */
            launcher_panel_hide(lp);
            return true;
        case 0xff8d: /* Return */
            if (lp->count > 0 && lp->selected >= 0 && lp->selected < lp->count)
                lp_activate(lp, lp->results[lp->selected]);
            return true;
        case 0xff8a: /* Up */
            if (lp->count > 0) lp->selected = (lp->selected - 1 + lp->count) % lp->count;
            panel_mark_dirty(p);
            return true;
        case 0xff54: /* Down */
            if (lp->count > 0) lp->selected = (lp->selected + 1) % lp->count;
            panel_mark_dirty(p);
            return true;
        case 0xff08: /* Backspace */
            if (lp->query[0]) {
                size_t n = strlen(lp->query);
                lp->query[n - 1] = '\0';
                lp_refresh(lp);
                panel_mark_dirty(p);
            }
            return true;
        case 0xffff: /* Delete */
            lp->query[0] = '\0';
            lp_refresh(lp);
            panel_mark_dirty(p);
            return true;
        default:
            break;
    }
    /* printable characters: UTF-8 bytes for the ASCII range plus latin-1 */
    if (keysym >= 0x20 && keysym <= 0x7e) {
        size_t n = strlen(lp->query);
        if (n + 2 < sizeof(lp->query)) {
            lp->query[n] = (char)keysym;
            lp->query[n + 1] = '\0';
            lp_refresh(lp);
            panel_mark_dirty(p);
        }
        return true;
    }
    return false;
}

static bool lp_click(struct panel *p, double x, double y)
{
    struct launcher_panel *lp = p->userdata;
    if (x < lp->row_x0 || x > lp->row_x0 + lp->row_w) return false;
    if (y < lp->row_y0) return false;
    int row = (int)((y - lp->row_y0) / LP_ROW_H);
    if (row < 0 || row >= lp->count) return false;
    lp_activate(lp, lp->results[row]);
    return true;
}

static void lp_motion(struct panel *p, double x, double y)
{
    struct launcher_panel *lp = p->userdata;
    if (x < lp->row_x0 || x > lp->row_x0 + lp->row_w || y < lp->row_y0) return;
    int row = (int)((y - lp->row_y0) / LP_ROW_H);
    if (row >= 0 && row < lp->count && row != lp->selected) {
        lp->selected = row;
        panel_mark_dirty(p);
    }
}

struct launcher_panel *launcher_panel_create(struct wayland_ctx *ctx, struct launcher *l,
                                             const struct config **cfg, struct output *out)
{
    struct launcher_panel *lp = calloc(1, sizeof(*lp));
    if (!lp) return NULL;
    panel_init(&lp->panel, ctx);
    lp->launcher = l;
    lp->fallback_out = out;
    lp->cfg = cfg ? *cfg : NULL;
    lp->selected = 0;
    lp_refresh(lp);
    panel_set_callbacks(&lp->panel, lp_draw, lp_key, lp_click, lp_motion, lp);
    return lp;
}

void launcher_panel_destroy(struct launcher_panel *lp)
{
    if (!lp) return;
    panel_fini(&lp->panel);
    free(lp);
}

struct panel *launcher_panel_surface(struct launcher_panel *lp)
{
    return lp ? &lp->panel : NULL;
}

void launcher_panel_show(struct launcher_panel *lp, struct output *out)
{
    if (!lp) return;
    if (!out) out = lp->fallback_out;
    lp->query[0] = '\0';
    lp_refresh(lp);
    if (!panel_show(&lp->panel, out, LP_W, LP_H)) log_warn("launcher panel: show failed");
}

void launcher_panel_hide(struct launcher_panel *lp)
{
    if (!lp) return;
    panel_hide(&lp->panel);
}

void launcher_panel_toggle(struct launcher_panel *lp, struct output *out)
{
    if (!lp) return;
    if (panel_is_visible(&lp->panel)) launcher_panel_hide(lp);
    else launcher_panel_show(lp, out);
}

bool launcher_panel_visible(struct launcher_panel *lp)
{
    return lp && panel_is_visible(&lp->panel);
}
bool launcher_panel_key_proxy(uint32_t keysym, uint32_t modifiers, void *userdata)
{
    struct launcher_panel *lp = userdata;
    if (!lp || !panel_is_visible(&lp->panel)) return false;
    return lp->panel.key(&lp->panel, keysym, modifiers);
}

void launcher_panel_toggle_proxy(void *userdata)
{
    struct launcher_panel *lp = userdata;
    if (!lp) return;
    struct output *out = lp->panel.out ? lp->panel.out : lp->fallback_out;
    launcher_panel_toggle(lp, out);
}
