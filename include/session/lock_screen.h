#ifndef EMANCIPATION_LOCK_SCREEN_H
#define EMANCIPATION_LOCK_SCREEN_H

#include <stdbool.h>

struct lock_screen;
struct wayland_ctx;

struct lock_screen *lock_screen_create(struct wayland_ctx *ctx);
void lock_screen_destroy(struct lock_screen *ls);
void lock_screen_lock(struct lock_screen *ls);
void lock_screen_unlock(struct lock_screen *ls);
bool lock_screen_is_locked(struct lock_screen *ls);

#endif /* EMANCIPATION_LOCK_SCREEN_H */
