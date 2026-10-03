#include "render/renderer.h"
#include "util/log.h"
#include <stdlib.h>

struct renderer {
    struct wayland_ctx *ctx;
    double scale;
    bool shared_gl;
};

struct renderer *renderer_create(struct wayland_ctx *ctx)
{
    if (!ctx) return NULL;
    struct renderer *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->ctx = ctx;
    r->scale = 1.0;
    r->shared_gl = true;
    log_debug("renderer created (ES stub)");
    return r;
}

void renderer_destroy(struct renderer *r)
{
    if (!r) return;
    free(r);
}

void renderer_begin_frame(struct renderer *r)
{
    (void)r;
}

void renderer_end_frame(struct renderer *r)
{
    (void)r;
}

void renderer_set_scale(struct renderer *r, double scale)
{
    if (!r) return;
    if (scale < 0.5) scale = 0.5;
    if (scale > 3.0) scale = 3.0;
    r->scale = scale;
}

double renderer_get_scale(struct renderer *r)
{
    if (!r) return 1.0;
    return r->scale;
}
