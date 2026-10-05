#ifndef EMANCIPATION_BAR_H
#define EMANCIPATION_BAR_H

#include <cairo.h>
#include <wayland-client.h>

struct config;
struct workspace_manager;

/* Paint the bar contents. `w`/`h` are logical sizes; the caller is
 * expected to have applied the buffer scale to `cr` already. */
void bar_draw(cairo_t *cr, int w, int h, const struct config *cfg,
              struct workspace_manager *wm, struct wl_output *output);

#endif /* EMANCIPATION_BAR_H */
