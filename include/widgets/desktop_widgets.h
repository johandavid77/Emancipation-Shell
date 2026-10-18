#ifndef EMANCIPATION_DESKTOP_WIDGETS_H
#define EMANCIPATION_DESKTOP_WIDGETS_H

#include <stdbool.h>

struct desktop_widgets;
struct wayland_ctx;
struct output;
struct config;
struct panel;
struct sysinfo_state;

/* Desktop widgets live on their own layer-shell surface on the *bottom* layer,
 * so they sit above the wallpaper but behind every window (mirrors Noctalia's
 * desktop widgets). */
struct desktop_widgets *desktop_widgets_create(struct wayland_ctx *ctx, struct output *out,
                                               const struct config *cfg);
void desktop_widgets_destroy(struct desktop_widgets *dw);
void desktop_widgets_update_config(struct desktop_widgets *dw, const struct config *cfg);
struct panel *desktop_widgets_surface(struct desktop_widgets *dw);
void desktop_widgets_mark_dirty(struct desktop_widgets *dw);
void desktop_widgets_set_sysinfo(struct desktop_widgets *dw, const struct sysinfo_state *sys);
bool desktop_widgets_is_visible(struct desktop_widgets *dw);

#endif /* EMANCIPATION_DESKTOP_WIDGETS_H */
