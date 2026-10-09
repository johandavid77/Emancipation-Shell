#include <cairo.h>
#ifndef EMANCIPATION_SYSTRAY_H
#define EMANCIPATION_SYSTRAY_H

struct systray;
struct wayland_ctx;

struct systray *systray_create(struct wayland_ctx *ctx);
void systray_destroy(struct systray *s);
int systray_item_count(const struct systray *s);
double systray_width(const struct systray *s); /* for bar layout */
void systray_draw(struct systray *s, cairo_t *cr, int x, int y, int h);

#endif
