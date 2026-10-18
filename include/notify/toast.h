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

/* Called when the visible toast goes away. reason matches the
 * org.freedesktop.Notifications NotificationClosed reasons:
 * 1 = expired, 2 = dismissed by the user. */
typedef void (*toast_done_fn)(void *userdata, unsigned int id, unsigned int reason);
void toast_set_done_cb(struct toast_panel *t, toast_done_fn fn, void *userdata);
unsigned int toast_current_id(struct toast_panel *t);

#endif
