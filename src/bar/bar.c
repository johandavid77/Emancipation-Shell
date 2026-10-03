#include "bar/bar.h"
#include "core/wayland.h"
#include "core/output.h"
#include "core/surface_mgr.h"
#include "config/config.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>

struct bar {
    struct wayland_ctx *ctx;
    struct output *out;
    struct surface_mgr *smgr;
    const struct config *cfg;
    bool visible;
    int height;
};

struct bar *bar_create(struct wayland_ctx *ctx, struct surface_mgr *smgr, struct output *out, const struct config *cfg)
{
    if (!ctx || !out || !cfg) return NULL;
    struct bar *b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->ctx = ctx;
    b->out = out;
    b->smgr = smgr;
    b->cfg = cfg;
    b->visible = cfg->bar.visible;
    b->height = cfg->bar.height;
    log_debug("bar created for output");
    return b;
}

void bar_destroy(struct bar *b)
{
    if (!b) return;
    free(b);
}

void bar_update_config(struct bar *b, const struct config *cfg)
{
    if (!b || !cfg) return;
    b->cfg = cfg;
    b->visible = cfg->bar.visible;
    b->height = cfg->bar.height;
}

void bar_relayout(struct bar *b)
{
    (void)b;
}

bool bar_is_visible(struct bar *b)
{
    if (!b) return false;
    return b->visible;
}
