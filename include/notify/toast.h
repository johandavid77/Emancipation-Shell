#ifndef EMANCIPATION_TOAST_H
#define EMANCIPATION_TOAST_H

#include "notify/notification.h"

struct toast_panel;
struct wayland_ctx;
struct output;

struct toast_panel *toast_create(struct wayland_ctx *ctx, struct output *out);
void toast_destroy(struct toast_panel *t);
void toast_show(struct toast_panel *t, const struct notification *n, int timeout_ms);
void toast_tick(struct toast_panel *t);

#endif
