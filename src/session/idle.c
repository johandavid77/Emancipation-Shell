#include "session/idle.h"
#include "session/lock_screen.h"
#include "core/wayland.h"
#include "core/seat.h"
#include "config/config.h"
#include "util/log.h"
#include "ext-idle-notify-v1-client-protocol.h"
#include <stdlib.h>
#include <string.h>

struct idle_watch {
    struct ext_idle_notification_v1 *obj;
    bool lock; /* true: lock session, false: screen off */
};

struct idle_manager {
    struct wayland_ctx *ctx;
    struct lock_screen *lock;
    struct idle_watch lock_watch;
    struct idle_watch off_watch;
    idle_screen_off_fn screen_off;
    void *screen_off_ud;
};

static void watch_idled(void *data, struct ext_idle_notification_v1 *n)
{
    (void)n;
    struct idle_watch *w = data;
    struct idle_manager *im = (struct idle_manager *)((char *)w -
        (w->lock ? offsetof(struct idle_manager, lock_watch) : offsetof(struct idle_manager, off_watch)));
    if (w->lock) {
        log_info("idle: locking session");
        if (im->lock) lock_screen_lock(im->lock);
    } else {
        log_info("idle: turning off monitors");
        if (im->screen_off) im->screen_off(im->screen_off_ud);
    }
}

static void watch_resumed(void *data, struct ext_idle_notification_v1 *n)
{
    (void)n;
    (void)data;
}

static const struct ext_idle_notification_v1_listener watch_listener = {
    .idled = watch_idled,
    .resumed = watch_resumed,
};

static void watch_arm(struct idle_manager *im, struct idle_watch *w, int seconds, bool lock)
{
    w->lock = lock;
    struct wl_seat *seat = seat_wl_seat(im->ctx->seat);
    if (!seat || seconds <= 0) return;
    w->obj = ext_idle_notifier_v1_get_idle_notification(im->ctx->idle_notifier,
                                                        (uint32_t)seconds * 1000, seat);
    if (w->obj) ext_idle_notification_v1_add_listener(w->obj, &watch_listener, w);
    log_info("idle: %s after %ds", lock ? "lock" : "screen-off", seconds);
}

static void watch_disarm(struct idle_watch *w)
{
    if (w->obj) { ext_idle_notification_v1_destroy(w->obj); w->obj = NULL; }
}

struct idle_manager *idle_manager_create(struct wayland_ctx *ctx, struct lock_screen *lock)
{
    if (!ctx) return NULL;
    struct idle_manager *im = calloc(1, sizeof(*im));
    if (!im) return NULL;
    im->ctx = ctx;
    im->lock = lock;
    return im;
}

void idle_manager_destroy(struct idle_manager *im)
{
    if (!im) return;
    watch_disarm(&im->lock_watch);
    watch_disarm(&im->off_watch);
    free(im);
}

void idle_manager_set_screen_off(struct idle_manager *im, idle_screen_off_fn fn, void *userdata)
{
    if (!im) return;
    im->screen_off = fn;
    im->screen_off_ud = userdata;
}

void idle_manager_apply(struct idle_manager *im, const struct config *cfg)
{
    if (!im || !cfg) return;
    watch_disarm(&im->lock_watch);
    watch_disarm(&im->off_watch);
    if (!cfg->idle.enabled || !im->ctx->idle_notifier) return;
    if (cfg->lock.enabled)
        watch_arm(im, &im->lock_watch, cfg->idle.lock_timeout, true);
    watch_arm(im, &im->off_watch, cfg->idle.screen_off_timeout, false);
}
