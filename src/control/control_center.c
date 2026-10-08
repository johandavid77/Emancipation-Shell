#include "control/control_center.h"
#include "core/output.h"
#include "core/panel.h"
#include "config/config.h"
#include "util/log.h"
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define CC_W 380
#define CC_H 460
#define CC_PAD 16.0
#define CC_ROW_H 36.0

enum cc_section { CC_HOME, CC_WINDOWS, CC_SYSTEM, CC_MAX };

struct control_center {
    struct panel panel;
    struct output *fallback_out;
    const struct config *cfg;
    enum cc_section section;
    int selected_idx;
    char title[64];
};



static void cc_draw(struct panel *p, cairo_t *cr, int w, int h)
{
    struct control_center *cc = p->userdata;
    cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 0.98);
    cairo_paint(cr);
    cairo_set_source_rgba(cr, 1,1,1,0.9);
    cairo_move_to(cr, CC_PAD, CC_PAD + 14);
    const char *titles[] = {"Home", "Windows", "System"};
    cairo_show_text(cr, titles[cc->section % CC_MAX]);
}


static bool cc_key(struct panel *p, uint32_t keysym, uint32_t mods)
{
    (void)mods;
    struct control_center *cc = p->userdata;
    if (keysym == XKB_KEY_Escape) {
        panel_hide(p);
        return true;
    }
    return true;
}

static bool cc_click(struct panel *p, double x, double y)
{
    (void)p; (void)x; (void)y;
    return true;
}

static void cc_motion(struct panel *p, double x, double y)
{
    (void)p; (void)x; (void)y;
}

struct control_center *control_center_create(struct wayland_ctx *ctx, const struct config **cfg,
                                             struct output *out)
{
    (void)cfg;
    struct control_center *cc = calloc(1, sizeof(*cc));
    if (!cc) return NULL;
    panel_init(&cc->panel, ctx);
    cc->cfg = cfg ? *cfg : NULL;
    cc->fallback_out = out;
    panel_set_callbacks(&cc->panel, cc_draw, cc_key, cc_click, cc_motion, cc);
    log_info("control center created");
    return cc;
}

void control_center_destroy(struct control_center *cc)
{
    if (!cc) return;
    panel_fini(&cc->panel);
    free(cc);
}

struct panel *control_center_surface(struct control_center *cc)
{
    return cc ? &cc->panel : NULL;
}

void control_center_toggle_proxy(void *userdata)
{
    struct control_center *cc = userdata;
    if (!cc) return;
    if (panel_is_visible(&cc->panel)) {
        panel_hide(&cc->panel);
        return;
    }
    struct output *out = cc->panel.out ? cc->panel.out : cc->fallback_out;
    if (!panel_show(&cc->panel, out, CC_W, CC_H)) log_warn("control center: show failed");
}
