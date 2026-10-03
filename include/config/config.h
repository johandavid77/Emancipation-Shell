#ifndef EMANCIPATION_CONFIG_H
#define EMANCIPATION_CONFIG_H

#include <stdbool.h>

struct color_rgba {
    float r;
    float g;
    float b;
    float a;
};

struct bar_config {
    int height;
    bool visible;
};

struct theme_config {
    struct color_rgba background;
    struct color_rgba foreground;
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

#endif /* EMANCIPATION_CONFIG_H */
