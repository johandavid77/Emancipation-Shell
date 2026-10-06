#ifndef EMANCIPATION_CONFIG_H
#define EMANCIPATION_CONFIG_H

#include <stdbool.h>

#define BAR_MAX_MODULES 12
#define BAR_MODULE_NAME 24

struct color_rgba {
    float r;
    float g;
    float b;
    float a;
};

enum bar_section { BAR_START = 0, BAR_CENTER, BAR_END, BAR_SECTIONS };

struct bar_config {
    int height;
    bool visible;
    int padding;  /* horizontal padding at both ends */
    int spacing;  /* gap between widgets */
    char clock_format[64];
    char date_format[64];
    char modules[BAR_SECTIONS][BAR_MAX_MODULES][BAR_MODULE_NAME];
    int n_modules[BAR_SECTIONS];
};

/* Palette roles follow Noctalia's material-like naming. */
struct theme_config {
    struct color_rgba background;      /* surface */
    struct color_rgba foreground;      /* on_surface */
    struct color_rgba primary;
    struct color_rgba on_primary;
    struct color_rgba secondary;
    struct color_rgba on_secondary;
    struct color_rgba error;
    struct color_rgba on_error;
    struct color_rgba surface_variant;
    struct color_rgba on_surface_variant;
};

struct font_config {
    char family[256];
    int size;
};

struct config {
    struct bar_config bar;
    struct theme_config theme;
    struct font_config font;
};

void config_defaults(struct config *cfg);
bool config_equal(const struct config *a, const struct config *b);
/* "#rrggbb" or "#rrggbbaa" */
bool color_from_hex(const char *hex, struct color_rgba *out);

#endif /* EMANCIPATION_CONFIG_H */
