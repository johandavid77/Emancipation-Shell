#include "plugins/plugin.h"
#include <stdlib.h>

#define PLUGIN_MAX 32

struct plugin_entry {
    struct plugin *p;
    bool inited;
};

struct plugin_registry {
    struct plugin_entry entries[PLUGIN_MAX];
    int n;
};

void plugin_registry_init(struct plugin_registry **reg)
{
    if (!reg) return;
    *reg = calloc(1, sizeof(struct plugin_registry));
}

void plugin_registry_fini(struct plugin_registry **reg)
{
    if (!reg || !*reg) return;
    free(*reg);
    *reg = NULL;
}

bool plugin_register(struct plugin_registry *reg, struct plugin *p)
{
    if (!reg || !p) return false;
    if (reg->n >= PLUGIN_MAX) return false;
    reg->entries[reg->n].p = p;
    reg->entries[reg->n].inited = false;
    reg->n++;
    return true;
}

int plugin_init_all(struct plugin_registry *reg, void *ctx)
{
    if (!reg) return 0;
    int ok = 0;
    for (int i = 0; i < reg->n; i++) {
        if (!reg->entries[i].inited && reg->entries[i].p->init) {
            if (reg->entries[i].p->init(ctx) == 0) {
                reg->entries[i].inited = true;
                ok++;
            }
        }
    }
    return ok;
}

void plugin_fini_all(struct plugin_registry *reg)
{
    if (!reg) return;
    for (int i = reg->n - 1; i >= 0; i--) {
        if (reg->entries[i].inited && reg->entries[i].p->fini) {
            reg->entries[i].p->fini();
            reg->entries[i].inited = false;
        }
    }
}
