#include "systray/systray.h"
#include "util/log.h"
#include <stdlib.h>

struct systray {
    int dummy;
};

struct systray *systray_create(struct wayland_ctx *ctx)
{
    (void)ctx;
    struct systray *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    log_debug("systray created (stub)");
    return s;
}

void systray_destroy(struct systray *s)
{
    if (!s) return;
    free(s);
}

int systray_item_count(const struct systray *s)
{
    (void)s;
    return 0;
}

double systray_width(const struct systray *s)
{
    (void)s;
    return 0.0;
}

void systray_draw(struct systray *s, cairo_t *cr, int x, int y, int h)
{
    (void)s; (void)cr; (void)x; (void)y; (void)h;
}
