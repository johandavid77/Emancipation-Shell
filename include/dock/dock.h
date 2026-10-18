#ifndef EMANCIPATION_DOCK_H
#define EMANCIPATION_DOCK_H

#include <wayland-client.h>
#include <stdbool.h>

struct dock;
struct wayland_ctx;
struct output;
struct config;
struct panel;
struct niri_ipc;
struct launcher;

struct dock *dock_create(struct wayland_ctx *ctx, struct output *out, const struct config *cfg);
void dock_destroy(struct dock *d);
void dock_update_config(struct dock *d, const struct config *cfg);
void dock_show(struct dock *d);
void dock_hide(struct dock *d);
bool dock_is_visible(struct dock *d);

/* Overlay panel handed to surface_mgr so pointer events route here. */
struct panel *dock_surface(struct dock *d);
/* Repaint (e.g. after the niri window list changed). */
void dock_mark_dirty(struct dock *d);

void dock_set_niri(struct dock *d, struct niri_ipc *niri);
void dock_set_launcher(struct dock *d, struct launcher *launcher);
/* Callback fired when the launcher button (left-most) is clicked. */
void dock_set_launcher_toggle(struct dock *d, void (*fn)(void *), void *userdata);

#endif /* EMANCIPATION_DOCK_H */
