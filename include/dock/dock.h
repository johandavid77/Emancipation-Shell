#ifndef EMANCIPATION_DOCK_H
#define EMANCIPATION_DOCK_H

#include <wayland-client.h>
#include <stdbool.h>

struct dock;
struct wayland_ctx;
struct output;
struct config;

struct dock *dock_create(struct wayland_ctx *ctx, struct output *out, const struct config *cfg);
void dock_destroy(struct dock *d);
void dock_update_config(struct dock *d, const struct config *cfg);
void dock_show(struct dock *d);
void dock_hide(struct dock *d);
bool dock_is_visible(struct dock *d);

#endif /* EMANCIPATION_DOCK_H */
