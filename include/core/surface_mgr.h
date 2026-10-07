#ifndef EMANCIPATION_SURFACE_MGR_H
#define EMANCIPATION_SURFACE_MGR_H

#include <wayland-client.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "bar/bar.h"

struct wayland_ctx;
struct output;
struct config;
struct workspace_manager;
struct sysinfo_state;
struct panel;
struct niri_ipc;

/* Called when the bar launcher button is clicked (opens the launcher panel). */
typedef void (*surface_mgr_launcher_fn)(void *userdata);

struct shm_buf {
    struct wl_buffer *wl;
    void *data;
    size_t size;
    int width;  /* pixels */
    int height; /* pixels */
    bool busy;  /* held by the compositor until wl_buffer.release */
};

struct layer_surface {
    struct surface_mgr *mgr;
    struct zwlr_layer_surface_v1 *ls;
    struct wl_surface *surf;
    struct output *out;
    struct wl_callback *frame_cb;
    struct shm_buf bufs[2];
    int width;  /* logical, from configure */
    int height; /* logical, from configure */
    bool configured;
    bool dirty;
    struct bar_hits hits; /* clickable spans from the last draw */
    double hover_x;       /* < 0 when the pointer is elsewhere */
    struct wl_list link;
};

struct surface_mgr {
    struct wayland_ctx *ctx;
    struct wl_list layers;   /* layer_surface.link */
    struct wl_list *outputs; /* owned by main */
    const struct config *cfg;
    struct workspace_manager *wm;
    const struct sysinfo_state *sys;
    struct launcher *launcher;
    struct niri_ipc *niri;
#define SURFACE_MGR_MAX_PANELS 4
    struct panel *panels[SURFACE_MGR_MAX_PANELS]; /* overlay panels, routed first for input */
    int n_panels;
    surface_mgr_launcher_fn on_launcher;
    void *on_launcher_userdata;
    surface_mgr_launcher_fn on_calendar;
    void *on_calendar_userdata;
    struct output *focus_output;
    int bar_height;
    bool shutdown;
};

void surface_mgr_init(struct surface_mgr *mgr, struct wayland_ctx *ctx, struct wl_list *outputs);
void surface_mgr_fini(struct surface_mgr *mgr);
void surface_mgr_set_sources(struct surface_mgr *mgr, const struct config *cfg, struct workspace_manager *wm);
void surface_mgr_apply_config(struct surface_mgr *mgr, const struct config *cfg);
void surface_mgr_request_redraw(struct surface_mgr *mgr);
void surface_mgr_mark_dirty_all(struct surface_mgr *mgr);
void surface_mgr_on_output_added(struct surface_mgr *mgr, struct output *out);
void surface_mgr_on_output_removed(struct surface_mgr *mgr, struct output *out);
void surface_mgr_set_sysinfo(struct surface_mgr *mgr, const struct sysinfo_state *sys);
void surface_mgr_set_launcher(struct surface_mgr *mgr, struct launcher *launcher);
void surface_mgr_set_launcher_toggle(struct surface_mgr *mgr, surface_mgr_launcher_fn fn, void *userdata);
/* Registers an overlay panel; input is routed to it before the bar. */
void surface_mgr_add_panel(struct surface_mgr *mgr, struct panel *p);
void surface_mgr_set_niri(struct surface_mgr *mgr, struct niri_ipc *n);
void surface_mgr_set_calendar_toggle(struct surface_mgr *mgr, surface_mgr_launcher_fn fn, void *userdata);
struct output *surface_mgr_first_output(struct surface_mgr *mgr);
/* Routes a keysym to the visible panel (launcher or calendar). */
bool surface_mgr_handle_key(struct surface_mgr *mgr, uint32_t keysym, uint32_t mods);

/* Pointer routing (from seat.c). Return true if a clickable item is under x. */
bool surface_mgr_pointer_motion(struct surface_mgr *mgr, struct wl_surface *surf, double x, double y);
void surface_mgr_pointer_leave(struct surface_mgr *mgr, struct wl_surface *surf);
void surface_mgr_pointer_click(struct surface_mgr *mgr, struct wl_surface *surf, double x, double y);
void surface_mgr_pointer_scroll(struct surface_mgr *mgr, struct wl_surface *surf, int dir);

#endif /* EMANCIPATION_SURFACE_MGR_H */
