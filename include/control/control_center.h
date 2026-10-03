#ifndef EMANCIPATION_CONTROL_CENTER_H
#define EMANCIPATION_CONTROL_CENTER_H

#include <stdbool.h>

struct control_center;
struct wayland_ctx;
struct output;

struct control_center *control_center_create(struct wayland_ctx *ctx, struct output *out);
void control_center_destroy(struct control_center *cc);
void control_center_show(struct control_center *cc);
void control_center_hide(struct control_center *cc);
void control_center_toggle(struct control_center *cc);
bool control_center_is_visible(struct control_center *cc);
void control_center_set_toggle(struct control_center *cc, const char *name, bool enabled);

#endif /* EMANCIPATION_CONTROL_CENTER_H */
