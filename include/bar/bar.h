#ifndef EMANCIPATION_BAR_H
#define EMANCIPATION_BAR_H

#include <wayland-client.h>
#include <stdbool.h>

struct bar;
struct wayland_ctx;
struct output;
struct surface_mgr;
struct config;

struct bar *bar_create(struct wayland_ctx *ctx, struct surface_mgr *smgr, struct output *out, const struct config *cfg);
void bar_destroy(struct bar *b);
void bar_update_config(struct bar *b, const struct config *cfg);
void bar_relayout(struct bar *b);
bool bar_is_visible(struct bar *b);

#endif /* EMANCIPATION_BAR_H */
