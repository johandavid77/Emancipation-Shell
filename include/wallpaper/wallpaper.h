#ifndef EMANCIPATION_WALLPAPER_H
#define EMANCIPATION_WALLPAPER_H

#include <stdbool.h>
#include "config/config.h"

struct wallpaper;
struct wayland_ctx;
struct output;

struct wallpaper *wallpaper_create(struct wayland_ctx *ctx, struct output *out);
void wallpaper_destroy(struct wallpaper *wp);
void wallpaper_set_path(struct wallpaper *wp, const char *path);
void wallpaper_set_dir(struct wallpaper *wp, const char *dir);
const char *wallpaper_get_path(const struct wallpaper *wp);
const char *wallpaper_get_dir(const struct wallpaper *wp);
void wallpaper_autodetect(struct wallpaper *wp);
void wallpaper_set_fill(struct wallpaper *wp, enum wp_fill_mode fill);
enum wp_fill_mode wallpaper_get_fill(const struct wallpaper *wp);
void wallpaper_reload(struct wallpaper *wp);
void wallpaper_next(struct wallpaper *wp);
void wallpaper_prev(struct wallpaper *wp);
void wallpaper_random(struct wallpaper *wp);
void wallpaper_set_enabled(struct wallpaper *wp, bool on);
/* Solid color painted when there is no image (theme background). */
void wallpaper_set_fallback(struct wallpaper *wp, double r, double g, double b);
/* Crossfade duration in ms between wallpapers (0 disables the transition). */
void wallpaper_set_transition(struct wallpaper *wp, int ms);
struct wallpaper *wallpaper_instance_get(void);

#endif /* EMANCIPATION_WALLPAPER_H */
