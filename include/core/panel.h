#ifndef EMANCIPATION_PANEL_H
#define EMANCIPATION_PANEL_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include "core/surface_mgr.h"

struct wayland_ctx;
struct output;

/* A panel is an overlay surface independent from the bar: launcher, control
 * center, OSD. Noctalia models the same thing as a `Panel` with its own
 * layer-shell surface (shell/launcher/launcher_panel.h), so we keep panels out
 * of the bar surface instead of painting over it. */

struct panel;

typedef void (*panel_draw_fn)(struct panel *p, cairo_t *cr, int w, int h);
/* Return true when the key was consumed. */
typedef bool (*panel_key_fn)(struct panel *p, uint32_t keysym, uint32_t modifiers);
typedef bool (*panel_click_fn)(struct panel *p, double x, double y);
typedef void (*panel_motion_fn)(struct panel *p, double x, double y);

struct panel {
    struct wayland_ctx *ctx;
    struct output *out;
    struct wl_surface *surf;
    struct zwlr_layer_surface_v1 *ls;
    struct wl_callback *frame_cb;
    struct shm_buf bufs[2];
    int w, h;   /* logical */
    bool configured;
    bool dirty;
    bool visible;
    struct wl_list link;

    int logical_w, logical_h;
    double hover_x, hover_y; /* pointer position, logical */

    panel_draw_fn draw;
    panel_key_fn key;
    panel_click_fn click;
    panel_motion_fn motion;
    void *userdata;
};

void panel_init(struct panel *p, struct wayland_ctx *ctx);
void panel_fini(struct panel *p);
void panel_set_callbacks(struct panel *p, panel_draw_fn draw, panel_key_fn key, panel_click_fn click,
                         panel_motion_fn motion, void *userdata);
bool panel_show(struct panel *p, struct output *out, int w, int h);
void panel_hide(struct panel *p);
bool panel_is_visible(const struct panel *p);
void panel_mark_dirty(struct panel *p);
void panel_present(struct panel *p);

#endif /* EMANCIPATION_PANEL_H */