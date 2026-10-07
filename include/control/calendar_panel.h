#ifndef EMANCIPATION_CALENDAR_PANEL_H
#define EMANCIPATION_CALENDAR_PANEL_H

#include "core/panel.h"

struct config;
struct output;

struct calendar_panel *calendar_panel_create(struct wayland_ctx *ctx, const struct config **cfg,
                                             struct output *out);
void calendar_panel_destroy(struct calendar_panel *cp);
struct panel *calendar_panel_surface(struct calendar_panel *cp);
void calendar_panel_toggle_proxy(void *userdata);

#endif /* EMANCIPATION_CALENDAR_PANEL_H */