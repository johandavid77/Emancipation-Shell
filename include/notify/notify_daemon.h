#ifndef EMANCIPATION_NOTIFY_DAEMON_H
#define EMANCIPATION_NOTIFY_DAEMON_H

#include <stdbool.h>

struct toast_panel;

struct notify_daemon;
struct wayland_ctx;
struct notification_store;

struct notify_daemon *notify_daemon_create(struct wayland_ctx *ctx);
void notify_daemon_destroy(struct notify_daemon *nd);
bool notify_daemon_is_running(struct notify_daemon *nd);
struct notification_store *notify_daemon_store(struct notify_daemon *nd);

/* Route incoming notifications to this toast panel. */
void notify_daemon_set_toast(struct notify_daemon *nd, struct toast_panel *t);

/* Main loop integration: D-Bus connection fd + dispatch. */
int notify_daemon_fd(struct notify_daemon *nd);
bool notify_daemon_dispatch(struct notify_daemon *nd);

#endif /* EMANCIPATION_NOTIFY_DAEMON_H */
