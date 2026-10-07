#define _GNU_SOURCE
#include "launcher/icons.h"
#include "util/log.h"

#include <cairo.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Icon lookup follows the freedesktop layout but only renders raster icons:
 * cairo can decode PNG directly, and librsvg is not a dependency. Entries whose
 * Icon= points at an SVG simply fall back to the first letter, which is what the
 * launcher drew before. */

static const char *icon_roots[] = {
    "/usr/share/icons",
    "/usr/local/share/icons",
    "/usr/share/pixmaps",
};

static const char *icon_themes[] = {
    "hicolor", "breeze", "breeze-dark", "Adwaita", "gnome", "oxygen", "Papirus",
};

static const int icon_sizes[] = {128, 96, 64, 48, 32, 24, 16};

static bool file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static bool dir_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* An absolute Icon= is used verbatim. */
static char *resolve_absolute(const char *icon)
{
    if (icon[0] != '/') return NULL;
    static const char *exts[] = {".png", ".PNG", NULL};
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", icon);
    if (file_exists(buf)) return strdup(buf);
    for (int i = 0; exts[i]; i++) {
        snprintf(buf, sizeof(buf), "%s%s", icon, exts[i]);
        if (file_exists(buf)) return strdup(buf);
    }
    return NULL;
}

/* icons_dir is the base that holds theme directories (e.g. /usr/share/icons),
 * theme is the theme name; the pixmaps "theme" has no subdirectory at all. */
static char *resolve_in_dir(const char *icons_dir, const char *theme, const char *icon)
{
    char buf[1024];
    if (strcmp(theme, "pixmaps") == 0) {
        snprintf(buf, sizeof(buf), "%s/%s/%s.png", icons_dir, theme, icon);
        if (file_exists(buf)) return strdup(buf);
        snprintf(buf, sizeof(buf), "%s/%s.png", icons_dir, icon);
        if (file_exists(buf)) return strdup(buf);
        return NULL;
    }
    /* freedesktop raster layout: <icons_dir>/<theme>/<size>x<size>/apps/<icon>.png */
    for (size_t i = 0; i < sizeof(icon_sizes) / sizeof(icon_sizes[0]); i++) {
        snprintf(buf, sizeof(buf), "%s/%s/%dx%d/apps/%s.png", icons_dir, theme, icon_sizes[i],
                 icon_sizes[i], icon);
        if (file_exists(buf)) return strdup(buf);
    }
    /* scale variants: <icons_dir>/<theme>/apps/<size>x<size>/<icon>.png */
    for (int s = 16; s <= 128; s *= 2) {
        snprintf(buf, sizeof(buf), "%s/%s/apps/%dx%d/%s.png", icons_dir, theme, s, s, icon);
        if (file_exists(buf)) return strdup(buf);
    }
    return NULL;
}

static char *resolve_svg_free_icons(const char *icon)
{
    (void)icon;
    return NULL;
}

char *icon_resolve_path(const char *icon)
{
    if (!icon || !*icon) return NULL;
    if (strchr(icon, '/')) return resolve_absolute(icon);

    const char *home = getenv("HOME");
    char buf[1024];

    /* user's own icon theme wins, like gtk's lookup */
    if (home) {
        for (size_t t = 0; t < sizeof(icon_themes)/sizeof(icon_themes[0]); t++) {
            snprintf(buf, sizeof(buf), "%s/.local/share/icons", home);
            char *p = resolve_in_dir(buf, icon_themes[t], icon);
            if (p) return p;
        }
        snprintf(buf, sizeof(buf), "%s/.icons", home);
        char *p = resolve_in_dir(buf, "pixmaps", icon);
        if (p) return p;
    }

    for (size_t r = 0; r < sizeof(icon_roots)/sizeof(icon_roots[0]); r++) {
        for (size_t t = 0; t < sizeof(icon_themes)/sizeof(icon_themes[0]); t++) {
            char tdir[1024];
            snprintf(tdir, sizeof(tdir), "%s/%s", icon_roots[r], icon_themes[t]);
            if (!dir_exists(tdir)) continue;
            char *p = resolve_in_dir(icon_roots[r], icon_themes[t], icon);
            if (p) return p;
        }
        char *p = resolve_in_dir(icon_roots[r], "pixmaps", icon);
        if (p) return p;
    }
    return resolve_svg_free_icons(icon);
}

cairo_surface_t *icon_load_surface(const char *icon, int target_px)
{
    char *path = icon_resolve_path(icon);
    if (!path) return NULL;
    cairo_surface_t *s = cairo_image_surface_create_from_png(path);
    free(path);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(s);
        return NULL;
    }
    if (target_px > 0 &&
        (cairo_image_surface_get_width(s) > target_px ||
         cairo_image_surface_get_height(s) > target_px)) {
        /* downscale once at load time so the panel draw stays cheap */
        double sx = (double)target_px / cairo_image_surface_get_width(s);
        double sy = (double)target_px / cairo_image_surface_get_height(s);
        double s_min = sx < sy ? sx : sy;
        cairo_surface_t *tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target_px, target_px);
        cairo_t *cr = cairo_create(tmp);
        cairo_scale(cr, s_min, s_min);
        cairo_set_source_surface(cr, s, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_destroy(s);
        s = tmp;
    }
    return s;
}