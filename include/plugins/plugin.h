#ifndef EMANCIPATION_PLUGIN_H
#define EMANCIPATION_PLUGIN_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct plugin {
    const char *name;
    int (*init)(void *ctx);
    void (*fini)(void);
};

struct plugin_registry;

void plugin_registry_init(struct plugin_registry **reg);
void plugin_registry_fini(struct plugin_registry **reg);
bool plugin_register(struct plugin_registry *reg, struct plugin *p);
int  plugin_init_all(struct plugin_registry *reg, void *ctx);
void plugin_fini_all(struct plugin_registry *reg);

#ifdef __cplusplus
}
#endif

#endif /* EMANCIPATION_PLUGIN_H */
