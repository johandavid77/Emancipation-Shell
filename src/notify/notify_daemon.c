#include "notify/notify_daemon.h"
#include "util/log.h"
#include "notify/notification.h"
#include <stdlib.h>
#include <dbus/dbus.h>
#include <string.h>
#include <unistd.h>

struct notify_daemon {
    struct wayland_ctx *ctx;
    struct notification_store store;
    bool running;
};

struct notify_daemon *notify_daemon_create(struct wayland_ctx *ctx)
{
    if (!ctx) return NULL;
    struct notify_daemon *nd = calloc(1, sizeof(*nd));
    if (!nd) return NULL;
    nd->ctx = ctx;
    notify_store_init(&nd->store);
    nd->running = true;
    log_info("notification daemon ready (org.freedesktop.Notifications placeholder)");
    return nd;
}

void notify_daemon_destroy(struct notify_daemon *nd)
{
    if (!nd) return;
    notify_store_free(&nd->store);
    free(nd);
}

bool notify_daemon_is_running(struct notify_daemon *nd)
{
    if (!nd) return false;
    return nd->running;
}

struct notification_store *notify_daemon_store(struct notify_daemon *nd)
{
    if (!nd) return NULL;
    return &nd->store;
}
