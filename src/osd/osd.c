#include "osd/osd.h"
#include "core/output.h"
#include "util/log.h"
#include <stdlib.h>

struct osd {
    struct wayland_ctx *ctx;
    struct output *out;
    bool visible;
    enum osd_type type;
    int value;
};

struct osd *osd_create(struct wayland_ctx *ctx, struct output *out)
{
    if (!ctx || !out) return NULL;
    struct osd *o = calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->ctx = ctx;
    o->out = out;
    o->visible = false;
    o->type = OSD_NONE;
    o->value = 0;
    log_debug("osd created");
    return o;
}

void osd_destroy(struct osd *o)
{
    if (!o) return;
    free(o);
}

void osd_show(struct osd *o, enum osd_type t, int value)
{
    if (!o) return;
    o->visible = true;
    o->type = t;
    o->value = value;
    if (t == OSD_VOLUME) {
        log_info("osd volume: %d%% (auto-hide ~2s)", value);
    } else if (t == OSD_BRIGHTNESS) {
        log_info("osd brightness: %d%% (auto-hide ~2s)", value);
    }
}

void osd_hide(struct osd *o)
{
    if (!o) return;
    o->visible = false;
    o->type = OSD_NONE;
}

bool osd_is_visible(struct osd *o)
{
    if (!o) return false;
    return o->visible;
}
