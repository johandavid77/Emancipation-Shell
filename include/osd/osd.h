#ifndef EMANCIPATION_OSD_H
#define EMANCIPATION_OSD_H

#include <stdbool.h>

struct osd;
struct wayland_ctx;
struct output;

enum osd_type {
    OSD_NONE,
    OSD_VOLUME,
    OSD_BRIGHTNESS
};

struct osd *osd_create(struct wayland_ctx *ctx, struct output *out);
void osd_destroy(struct osd *o);
void osd_show(struct osd *o, enum osd_type t, int value);
void osd_hide(struct osd *o);
bool osd_is_visible(struct osd *o);

#endif /* EMANCIPATION_OSD_H */
