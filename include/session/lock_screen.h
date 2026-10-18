#ifndef EMANCIPATION_LOCK_SCREEN_H
#define EMANCIPATION_LOCK_SCREEN_H

#include <stdbool.h>
#include <wayland-client.h>

struct lock_screen;
struct wayland_ctx;
struct config;

struct lock_screen *lock_screen_create(struct wayland_ctx *ctx, struct wl_list *outputs);
void lock_screen_destroy(struct lock_screen *ls);
void lock_screen_set_config(struct lock_screen *ls, const struct config *cfg);
void lock_screen_lock(struct lock_screen *ls);
void lock_screen_unlock(struct lock_screen *ls);
bool lock_screen_is_locked(struct lock_screen *ls);
/* Master switch from config; false when locking is disabled (defaults true). */
bool lock_screen_enabled(struct lock_screen *ls);
/* Redraw the lock UI (e.g. clock tick); no-op unless locked. */
void lock_screen_refresh(struct lock_screen *ls);

#endif /* EMANCIPATION_LOCK_SCREEN_H */
