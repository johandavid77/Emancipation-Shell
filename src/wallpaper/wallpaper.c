#include "wallpaper/wallpaper.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>

struct wallpaper {
    struct wayland_ctx *ctx;
    struct output *out;
    char path[512];
    enum wp_fill fill;
};

struct wallpaper *wallpaper_create(struct wayland_ctx *ctx, struct output *out)
{
    if (!ctx || !out) return NULL;
    struct wallpaper *wp = calloc(1, sizeof(*wp));
    if (!wp) return NULL;
    wp->ctx = ctx;
    wp->out = out;
    wp->path[0] = '\0';
    wp->fill = WP_FILL_CROP;
    log_debug("wallpaper created");
    return wp;
}

void wallpaper_destroy(struct wallpaper *wp)
{
    if (!wp) return;
    free(wp);
}

void wallpaper_set_path(struct wallpaper *wp, const char *path)
{
    if (!wp || !path) return;
    strncpy(wp->path, path, sizeof(wp->path)-1);
    wp->path[sizeof(wp->path)-1] = '\0';
    log_info("wallpaper set: %s", wp->path);
}

void wallpaper_set_fill(struct wallpaper *wp, enum wp_fill fill)
{
    if (!wp) return;
    wp->fill = fill;
}

void wallpaper_reload(struct wallpaper *wp)
{
    if (!wp) return;
    log_info("wallpaper reload");
}
