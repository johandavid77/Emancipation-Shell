#include "dock/dock.h"
#include "core/output.h"
#include "config/config.h"
#include "util/log.h"
#include <stdlib.h>

struct dock {
    struct wayland_ctx *ctx;
    struct output *out;
    bool visible;
    int height;
};

struct dock *dock_create(struct wayland_ctx *ctx, struct output *out, const struct config *cfg)
{
    (void)cfg;
    if (!ctx || !out) return NULL;
    struct dock *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->ctx = ctx;
    d->out = out;
    d->visible = true;
    d->height = 48;
    log_debug("dock created");
    return d;
}

void dock_destroy(struct dock *d)
{
    if (!d) return;
    free(d);
}

void dock_update_config(struct dock *d, const struct config *cfg)
{
    (void)d;
    (void)cfg;
}

void dock_show(struct dock *d)
{
    if (!d) return;
    d->visible = true;
}

void dock_hide(struct dock *d)
{
    if (!d) return;
    d->visible = false;
}

bool dock_is_visible(struct dock *d)
{
    if (!d) return false;
    return d->visible;
}
