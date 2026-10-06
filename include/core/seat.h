#ifndef EMANCIPATION_SEAT_H
#define EMANCIPATION_SEAT_H

#include <wayland-client.h>

struct seat_state;
struct surface_mgr;
struct wayland_ctx;

struct seat_state *seat_create(struct wayland_ctx *ctx, struct surface_mgr *mgr);
/* Called by the registry when a wl_seat global is bound. */
void seat_attach(struct seat_state *s, struct wl_seat *wl_seat);
void seat_destroy(struct seat_state *s);

#endif /* EMANCIPATION_SEAT_H */
