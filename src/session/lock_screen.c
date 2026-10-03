#include "session/lock_screen.h"
#include "util/log.h"
#include <stdlib.h>

struct lock_screen {
    struct wayland_ctx *ctx;
    bool locked;
};

struct lock_screen *lock_screen_create(struct wayland_ctx *ctx)
{
    if (!ctx) return NULL;
    struct lock_screen *ls = calloc(1, sizeof(*ls));
    if (!ls) return NULL;
    ls->ctx = ctx;
    ls->locked = false;
    log_debug("lock screen created");
    return ls;
}

void lock_screen_destroy(struct lock_screen *ls)
{
    if (!ls) return;
    free(ls);
}

void lock_screen_lock(struct lock_screen *ls)
{
    if (!ls) return;
    ls->locked = true;
    log_info("session locked (ext-session-lock-v1)");
}

void lock_screen_unlock(struct lock_screen *ls)
{
    if (!ls) return;
    ls->locked = false;
    log_info("session unlocked");
}

bool lock_screen_is_locked(struct lock_screen *ls)
{
    if (!ls) return false;
    return ls->locked;
}
