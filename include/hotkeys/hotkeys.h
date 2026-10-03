#ifndef EMANCIPATION_HOTKEYS_H
#define EMANCIPATION_HOTKEYS_H

#include <stdbool.h>

struct hotkeys;

struct hotkeys *hotkeys_create(void);
void hotkeys_destroy(struct hotkeys *hk);
void hotkeys_register(struct hotkeys *hk, const char *key, const char *action);
void hotkeys_reload(struct hotkeys *hk);

#endif /* EMANCIPATION_HOTKEYS_H */
