#ifndef EMANCIPATION_CONTROL_CENTER_H
#define EMANCIPATION_CONTROL_CENTER_H

#include <stdbool.h>

struct control_center;
struct wayland_ctx;
struct output;
struct config;

struct control_center *control_center_create(struct wayland_ctx *ctx, const struct config **cfg,
                                             struct output *out);
void control_center_destroy(struct control_center *cc);
struct panel *control_center_surface(struct control_center *cc);
void control_center_toggle_proxy(void *userdata);

#endif /* EMANCIPATION_CONTROL_CENTER_H */
