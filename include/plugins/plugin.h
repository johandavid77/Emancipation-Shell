#ifndef EMANCIPATION_PLUGIN_H
#define EMANCIPATION_PLUGIN_H

#include <stdbool.h>

struct plugin {
    char name[128];
    bool loaded;
};

struct plugin_manager {
    struct plugin *plugins;
    int count;
    int capacity;
};

void plugin_mgr_init(struct plugin_manager *pm);
void plugin_mgr_free(struct plugin_manager *pm);
bool plugin_mgr_load(struct plugin_manager *pm, const char *name);
bool plugin_mgr_unload(struct plugin_manager *pm, const char *name);

#endif /* EMANCIPATION_PLUGIN_H */
