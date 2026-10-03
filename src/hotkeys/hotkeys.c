#include "hotkeys/hotkeys.h"
#include "util/log.h"
#include <stdlib.h>

struct hotkeys {
    int dummy;
};

struct hotkeys *hotkeys_create(void)
{
    struct hotkeys *hk = calloc(1, sizeof(*hk));
    if (!hk) return NULL;
    log_debug("hotkeys created");
    return hk;
}

void hotkeys_destroy(struct hotkeys *hk)
{
    if (!hk) return;
    free(hk);
}

void hotkeys_register(struct hotkeys *hk, const char *key, const char *action)
{
    (void)hk; (void)key; (void)action;
    log_debug("hotkey registered");
}

void hotkeys_reload(struct hotkeys *hk)
{
    (void)hk;
    log_info("hotkeys reloaded");
}
