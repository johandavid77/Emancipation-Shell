#ifndef EMANCIPATION_WALLPAPER_H
#define EMANCIPATION_WALLPAPER_H

#include <stdbool.h>

struct wallpaper;
struct wayland_ctx;
struct output;

enum wp_fill {
    WP_FILL_CENTER,
    WP_FILL_CROP,
    WP_FILL_FIT,
    WP_FILL_STRETCH,
    WP_FILL_REPEAT,
    WP_FILL_SPAN
};

struct wallpaper *wallpaper_create(struct wayland_ctx *ctx, struct output *out);
void wallpaper_destroy(struct wallpaper *wp);
void wallpaper_set_path(struct wallpaper *wp, const char *path);
void wallpaper_set_fill(struct wallpaper *wp, enum wp_fill fill);
void wallpaper_reload(struct wallpaper *wp);

#endif /* EMANCIPATION_WALLPAPER_H */
