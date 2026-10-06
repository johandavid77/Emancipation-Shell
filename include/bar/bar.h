#ifndef EMANCIPATION_BAR_H
#define EMANCIPATION_BAR_H

#include <cairo.h>
#include <stdbool.h>
#include <wayland-client.h>

struct config;
struct workspace_manager;
struct sysinfo_state;
struct launcher;

#define BAR_MAX_HITS 64

enum bar_hit_kind {
    BAR_HIT_WORKSPACE = 1,
    BAR_HIT_LAUNCHER,
};

/* Clickable horizontal span, in logical coordinates. */
struct bar_hit {
    double x0, x1;
    enum bar_hit_kind kind;
    void *ref; /* workspace handle for BAR_HIT_WORKSPACE */
};

struct bar_hits {
    struct bar_hit h[BAR_MAX_HITS];
    int n;
};

struct bar_ctx {
    const struct config *cfg;
    struct workspace_manager *wm;
    const struct sysinfo_state *sys;
    struct launcher *launcher;
    struct wl_output *output;
    double hover_x; /* pointer x over this bar, < 0 when outside */
};

/* Paint the bar. `w`/`h` are logical sizes; `cr` must already be scaled.
 * Clickable regions are written to `hits` (may be NULL). */
void bar_draw(cairo_t *cr, int w, int h, const struct bar_ctx *ctx, struct bar_hits *hits);

/* Return the hit under logical x, or NULL. */
const struct bar_hit *bar_hit_at(const struct bar_hits *hits, double x);

#endif /* EMANCIPATION_BAR_H */
