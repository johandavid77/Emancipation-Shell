#ifndef EMANCIPATION_MPRIS_H
#define EMANCIPATION_MPRIS_H

#include <stdbool.h>

struct backends_state;

/* MPRIS v2 client: tracks the active media player and publishes its state
 * into `struct backends_state` (media_*). Uses its own private session-bus
 * connection so it can be polled independently of the notification daemon. */
struct mpris;

struct mpris *mpris_create(struct backends_state *bk);
void mpris_destroy(struct mpris *m);

/* Main loop integration. */
int mpris_fd(struct mpris *m);
bool mpris_dispatch(struct mpris *m);

#endif /* EMANCIPATION_MPRIS_H */
