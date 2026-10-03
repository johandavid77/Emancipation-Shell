#include "plugins/plugin.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>

void plugin_mgr_init(struct plugin_manager *pm)
{
    if (!pm) return;
    pm->plugins = NULL;
    pm->count = 0;
    pm->capacity = 0;
}

void plugin_mgr_free(struct plugin_manager *pm)
{
    if (!pm) return;
    free(pm->plugins);
    pm->plugins = NULL;
    pm->count = pm->capacity = 0;
}

bool plugin_mgr_load(struct plugin_manager *pm, const char *name)
{
    if (!pm || !name) return false;
    if (pm->count >= pm->capacity) {
        int nc = pm->capacity == 0 ? 8 : pm->capacity * 2;
        struct plugin *np = realloc(pm->plugins, nc * sizeof(*np));
        if (!np) return false;
        pm->plugins = np;
        pm->capacity = nc;
    }
    strncpy(pm->plugins[pm->count].name, name, sizeof(pm->plugins[pm->count].name)-1);
    pm->plugins[pm->count].name[sizeof(pm->plugins[pm->count].name)-1] = '\0';
    pm->plugins[pm->count].loaded = true;
    pm->count++;
    log_info("plugin loaded: %s", name);
    return true;
}

bool plugin_mgr_unload(struct plugin_manager *pm, const char *name)
{
    if (!pm || !name) return false;
    for (int i = 0; i < pm->count; i++) {
        if (strcmp(pm->plugins[i].name, name) == 0 && pm->plugins[i].loaded) {
            pm->plugins[i].loaded = false;
            log_info("plugin unloaded: %s", name);
            return true;
        }
    }
    return false;
}
