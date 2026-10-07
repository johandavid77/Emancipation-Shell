#ifndef EMANCIPATION_SEAT_H
#define EMANCIPATION_SEAT_H

#include <wayland-client.h>
#include <stdbool.h>
#include <stdint.h>

struct seat_state;
struct surface_mgr;
struct wayland_ctx;

/* Key events are forwarded here; the handler returns true when consumed. */
typedef bool (*seat_key_fn)(uint32_t keysym, uint32_t modifiers, void *userdata);

struct seat_state *seat_create(struct wayland_ctx *ctx, struct surface_mgr *mgr);
void seat_set_key_handler(struct seat_state *s, seat_key_fn fn, void *userdata);
/* Called by the registry when a wl_seat global is bound. */
void seat_attach(struct seat_state *s, struct wl_seat *wl_seat);
void seat_destroy(struct seat_state *s);

#endif /* EMANCIPATION_SEAT_H */
