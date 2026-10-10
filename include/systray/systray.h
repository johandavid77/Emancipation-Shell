#ifndef EMANCIPATION_SYSTRAY_H
#define EMANCIPATION_SYSTRAY_H

#include <cairo.h>
#include <stdbool.h>

struct systray;
struct wayland_ctx;

/* Status Notifier Item host: owns org.kde.StatusNotifierWatcher and tracks
 * items apps register. Returns NULL when D-Bus is unavailable. */
struct systray *systray_create(struct wayland_ctx *ctx);
void systray_destroy(struct systray *s);

/* Poll integration. fd is -1 when unavailable. */
int systray_fd(struct systray *s);
/* Process pending D-Bus traffic. Returns true when the item list changed. */
bool systray_dispatch(struct systray *s);

int systray_item_count(struct systray *s);
/* Total logical width needed to draw all items, or 0 when empty. */
double systray_width(struct systray *s);
/* Draw items starting at x, vertically centered in a bar of height h. */
void systray_draw(struct systray *s, cairo_t *cr, double x, int h);

#endif /* EMANCIPATION_SYSTRAY_H */
