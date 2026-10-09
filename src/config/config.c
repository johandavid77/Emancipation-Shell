#include "config/config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool color_from_hex(const char *hex, struct color_rgba *out)
{
    if (!hex || !out) return false;
    if (*hex == '#') hex++;
    size_t len = strlen(hex);
    if (len != 6 && len != 8) return false;
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)hex[i])) return false;
    }
    unsigned long v = strtoul(hex, NULL, 16);
    if (len == 6) v = (v << 8) | 0xff;
    out->r = ((v >> 24) & 0xff) / 255.0f;
    out->g = ((v >> 16) & 0xff) / 255.0f;
    out->b = ((v >> 8) & 0xff) / 255.0f;
    out->a = (v & 0xff) / 255.0f;
    return true;
}

static void add_module(struct bar_config *b, enum bar_section s, const char *name)
{
    if (b->n_modules[s] >= BAR_MAX_MODULES) return;
    snprintf(b->modules[s][b->n_modules[s]++], BAR_MODULE_NAME, "%s", name);
}

void config_defaults(struct config *cfg)
{
    if (!cfg) return;
    /* zero everything (incl. padding) so config_equal can memcmp */
    memset(cfg, 0, sizeof(*cfg));

    /* geometry mirrors Noctalia v5 defaults (ui/style.h, config_types.h) */
    cfg->bar.height = 34;
    cfg->bar.visible = true;
    cfg->bar.padding = 14;
    cfg->bar.spacing = 12;
    snprintf(cfg->bar.clock_format, sizeof(cfg->bar.clock_format), "%%H:%%M");
    snprintf(cfg->bar.date_format, sizeof(cfg->bar.date_format), "%%a %%d %%b");
    add_module(&cfg->bar, BAR_START, "launcher");
    add_module(&cfg->bar, BAR_START, "workspaces");
    add_module(&cfg->bar, BAR_CENTER, "clock");
    add_module(&cfg->bar, BAR_END, "cpu");
    add_module(&cfg->bar, BAR_END, "ram");
    add_module(&cfg->bar, BAR_END, "battery");
    add_module(&cfg->bar, BAR_END, "tray");
    add_module(&cfg->bar, BAR_END, "date");

    /* builtin "Noctalia" dark palette (theme/builtin_palettes.cpp) */
    color_from_hex("#070722", &cfg->theme.background);
    color_from_hex("#f3edf7", &cfg->theme.foreground);
    color_from_hex("#fff59b", &cfg->theme.primary);
    color_from_hex("#0e0e43", &cfg->theme.on_primary);
    color_from_hex("#a9aefe", &cfg->theme.secondary);
    color_from_hex("#0e0e43", &cfg->theme.on_secondary);
    color_from_hex("#fd4663", &cfg->theme.error);
    color_from_hex("#0e0e43", &cfg->theme.on_error);
    color_from_hex("#11112d", &cfg->theme.surface_variant);
    color_from_hex("#7c80b4", &cfg->theme.on_surface_variant);
    /* Panels keep niri's neutral dark background instead of the tinted bar color,
     * so an overlay never reads as a blue slab. */
    color_from_hex("#1e1e1e", &cfg->theme.panel_background);

    snprintf(cfg->font.family, sizeof(cfg->font.family), "sans-serif");
    cfg->font.size = 11; /* points; ~14px at 96 dpi like Noctalia body text */
}

bool config_equal(const struct config *a, const struct config *b)
{
    if (!a || !b) return false;
    return memcmp(a, b, sizeof(*a)) == 0;
}
