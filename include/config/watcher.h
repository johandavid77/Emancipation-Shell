#ifndef EMANCIPATION_CONFIG_WATCHER_H
#define EMANCIPATION_CONFIG_WATCHER_H

#include <stdbool.h>
#include "config/config.h"

struct config_watcher;

typedef void (*config_changed_cb)(void *userdata, const struct config *cfg, bool applied);

struct config_watcher *config_watcher_create(const char *path, void *loop, void *userdata, config_changed_cb cb);
void config_watcher_destroy(struct config_watcher *w);
void config_watcher_reload_now(struct config_watcher *w);
const struct config *config_watcher_get_last(struct config_watcher *w);
bool config_watcher_has_error(struct config_watcher *w);
const char *config_watcher_last_error(struct config_watcher *w);

#endif /* EMANCIPATION_CONFIG_WATCHER_H */
