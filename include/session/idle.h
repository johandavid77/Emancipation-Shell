#ifndef EMANCIPATION_IDLE_H
#define EMANCIPATION_IDLE_H

struct idle_manager;
struct wayland_ctx;
struct lock_screen;
struct config;

typedef void (*idle_screen_off_fn)(void *userdata);

struct idle_manager *idle_manager_create(struct wayland_ctx *ctx, struct lock_screen *lock);
void idle_manager_destroy(struct idle_manager *im);
void idle_manager_set_screen_off(struct idle_manager *im, idle_screen_off_fn fn, void *userdata);
void idle_manager_apply(struct idle_manager *im, const struct config *cfg);

#endif /* EMANCIPATION_IDLE_H */
