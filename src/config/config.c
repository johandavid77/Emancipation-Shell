#include "config/config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

void theme_apply_palette(struct theme_config *th, bool dark)
{
    if (!th) return;
    if (dark) {
        color_from_hex("#070722", &th->background);
        color_from_hex("#f3edf7", &th->foreground);
        color_from_hex("#fff59b", &th->primary);
        color_from_hex("#0e0e43", &th->on_primary);
        color_from_hex("#a9aefe", &th->secondary);
        color_from_hex("#0e0e43", &th->on_secondary);
        color_from_hex("#fd4663", &th->error);
        color_from_hex("#0e0e43", &th->on_error);
        color_from_hex("#11112d", &th->surface_variant);
        color_from_hex("#7c80b4", &th->on_surface_variant);
        /* Panels keep niri's neutral dark background instead of the tinted bar
         * color, so an overlay never reads as a blue slab. */
        color_from_hex("#1e1e1e", &th->panel_background);
    } else {
        color_from_hex("#faf7ff", &th->background);
        color_from_hex("#1b1b21", &th->foreground);
        color_from_hex("#5b5bd6", &th->primary);
        color_from_hex("#ffffff", &th->on_primary);
        color_from_hex("#565f89", &th->secondary);
        color_from_hex("#ffffff", &th->on_secondary);
        color_from_hex("#ba1a1a", &th->error);
        color_from_hex("#ffffff", &th->on_error);
        color_from_hex("#e6e0ec", &th->surface_variant);
        color_from_hex("#4a4458", &th->on_surface_variant);
        color_from_hex("#ececed", &th->panel_background);
    }
    th->dark = dark;
}

void theme_apply_night(struct theme_config *th)
{
    if (!th) return;
    /* Warm shift: lift red, trim blue. Keeps hue relationships intact while
     * removing the harshest blue light, mirroring a low color temperature. */
    struct color_rgba *roles[] = {
        &th->background, &th->panel_background, &th->foreground, &th->primary,
        &th->on_primary, &th->secondary, &th->on_secondary, &th->error,
        &th->on_error, &th->surface_variant, &th->on_surface_variant,
    };
    for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); i++) {
        struct color_rgba *c = roles[i];
        c->r = c->r * 0.82f + 0.18f;
        if (c->r > 1.0f) c->r = 1.0f;
        c->g *= 0.94f;
        c->b *= 0.72f;
    }
}

void config_defaults(struct config *cfg)
{
    if (!cfg) return;
    /* zero everything (incl. padding) so config_equal can memcmp */
    memset(cfg, 0, sizeof(*cfg));

    /* geometry mirrors Noctalia v5 defaults (ui/style.h, config_types.h) */
    cfg->bar.height = 34;
    cfg->bar.visible = true;
    cfg->bar.show_weather = false;
    cfg->bar.padding = 14;
    cfg->bar.spacing = 12;
    snprintf(cfg->bar.clock_format, sizeof(cfg->bar.clock_format), "%%H:%%M");
    snprintf(cfg->bar.date_format, sizeof(cfg->bar.date_format), "%%a %%d %%b");
    add_module(&cfg->bar, BAR_START, "launcher");
    add_module(&cfg->bar, BAR_START, "workspaces");
    add_module(&cfg->bar, BAR_START, "taskbar");
    add_module(&cfg->bar, BAR_CENTER, "media");
    add_module(&cfg->bar, BAR_CENTER, "clock");
    add_module(&cfg->bar, BAR_END, "cpu");
    add_module(&cfg->bar, BAR_END, "ram");
    add_module(&cfg->bar, BAR_END, "battery");
    add_module(&cfg->bar, BAR_END, "network");
    add_module(&cfg->bar, BAR_END, "volume");
    add_module(&cfg->bar, BAR_END, "brightness");
    add_module(&cfg->bar, BAR_END, "clipboard");
    add_module(&cfg->bar, BAR_END, "tray");
    add_module(&cfg->bar, BAR_END, "weather");
    add_module(&cfg->bar, BAR_END, "date");
    add_module(&cfg->bar, BAR_END, "control_center");

    /* builtin "Noctalia" dark palette (theme/builtin_palettes.cpp) */
    theme_apply_palette(&cfg->theme, true);
    cfg->theme.dark = true;
    cfg->theme.night = false;

    snprintf(cfg->font.family, sizeof(cfg->font.family), "sans-serif");
    cfg->font.size = 11; /* points; ~14px at 96 dpi like Noctalia body text */

    cfg->wallpaper.path[0] = '\0';
    cfg->wallpaper.dir[0] = '\0';
    cfg->wallpaper.fill = WP_FILL_CROP;
    cfg->wallpaper.enabled = false;
    cfg->wallpaper.interval = 0;
    cfg->wallpaper.randomize = false;
    cfg->wallpaper.transition = 700;

    cfg->idle.enabled = true;
    cfg->idle.lock_timeout = 300;
    cfg->idle.screen_off_timeout = 600;

    cfg->lock.enabled = true;

    cfg->dock.enabled = true;
    cfg->dock.autohide = false;
    cfg->dock.height = 56;
    cfg->dock.icon_size = 40;
    cfg->dock.n_pinned = 0;

    cfg->widgets.enabled = false;
    cfg->widgets.clock = true;
    cfg->widgets.sysmon = true;
    cfg->widgets.position = 0;
    cfg->widgets.width = 260;
}

static const char *wp_fill_names[WP_FILL_MAX] = {
    "crop", "fit", "stretch", "center", "repeat", "span"
};

const char *wp_fill_to_str(enum wp_fill_mode m)
{
    if (m < 0 || m >= WP_FILL_MAX) return "crop";
    return wp_fill_names[m];
}

enum wp_fill_mode wp_fill_from_str(const char *s)
{
    if (!s) return WP_FILL_CROP;
    for (int i = 0; i < WP_FILL_MAX; i++) {
        if (strcasecmp(s, wp_fill_names[i]) == 0) return (enum wp_fill_mode)i;
    }
    return WP_FILL_CROP;
}

bool config_equal(const struct config *a, const struct config *b)
{
    if (!a || !b) return false;
    return memcmp(a, b, sizeof(*a)) == 0;
}
