#include "osd/osd.h"
#include "core/output.h"
#include "core/panel.h"
#include "util/log.h"
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>

#define OSD_W 280
#define OSD_H 60

struct osd {
    struct panel panel;
    struct output *fallback_out;
    enum osd_type type;
    int value;
    struct timespec hide_at;
};



static void osd_draw(struct panel *p, cairo_t *cr, int w, int h)
{
    struct osd *o = p->userdata;
    cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 0.94);
    cairo_paint(cr);
    cairo_set_source_rgba(cr, 1,1,1,0.92);
    char buf[64];
    if (o->type == OSD_VOLUME) {
        snprintf(buf, sizeof(buf), "Volume: %d%%", o->value);
    } else if (o->type == OSD_BRIGHTNESS) {
        snprintf(buf, sizeof(buf), "Brightness: %d%%", o->value);
    } else {
        buf[0] = 0;
    }
    cairo_move_to(cr, 16, (h - 16)/2);
    cairo_show_text(cr, buf);
}

static bool osd_key(struct panel *p, uint32_t keysym, uint32_t mods)
{
    (void)p; (void)keysym; (void)mods;
    return true;
}

static bool osd_click(struct panel *p, double x, double y)
{
    (void)p; (void)x; (void)y;
    panel_hide(p);
    return true;
}

static void osd_motion(struct panel *p, double x, double y)
{
    (void)p; (void)x; (void)y;
}

struct osd *osd_create(struct wayland_ctx *ctx, struct output *out)
{
    if (!ctx || !out) return NULL;
    struct osd *o = calloc(1, sizeof(*o));
    if (!o) return NULL;
    panel_init(&o->panel, ctx);
    o->fallback_out = out;
    o->type = OSD_NONE;
    panel_set_callbacks(&o->panel, osd_draw, osd_key, osd_click, osd_motion, o);
    log_debug("osd created");
    return o;
}

void osd_destroy(struct osd *o)
{
    if (!o) return;
    panel_fini(&o->panel);
    free(o);
}

void osd_show(struct osd *o, enum osd_type t, int value)
{
    if (!o) return;
    o->type = t;
    o->value = value > 100 ? 100 : (value < 0 ? 0 : value);
    struct output *out = o->panel.out ? o->panel.out : o->fallback_out;
    if (!panel_show(&o->panel, out, OSD_W, OSD_H)) {
        log_warn("osd: show failed");
        return;
    }
    /* center near top */
    /* panel_show centers; nudge? easier to call panel_show as is; compositor places center */
    log_info("osd %s: %d%%", t == OSD_VOLUME ? "volume" : "brightness", o->value);
}

void osd_hide(struct osd *o)
{
    if (!o) return;
    panel_hide(&o->panel);
    o->type = OSD_NONE;
}

bool osd_is_visible(struct osd *o)
{
    if (!o) return false;
    return panel_is_visible(&o->panel);
}
