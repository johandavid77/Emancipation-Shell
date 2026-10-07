#ifndef EMANCIPATION_LAUNCHER_PANEL_H
#define EMANCIPATION_LAUNCHER_PANEL_H

#include "core/panel.h"

struct launcher;
struct config;

struct launcher_panel *launcher_panel_create(struct wayland_ctx *ctx, struct launcher *l,
                                             const struct config **cfg);
void launcher_panel_destroy(struct launcher_panel *lp);
struct panel *launcher_panel_surface(struct launcher_panel *lp);
void launcher_panel_toggle(struct launcher_panel *lp, struct output *out);
void launcher_panel_show(struct launcher_panel *lp, struct output *out);
void launcher_panel_hide(struct launcher_panel *lp);
bool launcher_panel_visible(struct launcher_panel *lp);
/* seat_key_fn adapter: forwards keysym/state to the panel while it is open. */
bool launcher_panel_key_proxy(uint32_t keysym, uint32_t modifiers, void *userdata);
void launcher_panel_toggle_proxy(void *userdata);

#endif /* EMANCIPATION_LAUNCHER_PANEL_H */