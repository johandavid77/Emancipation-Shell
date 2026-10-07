#ifndef EMANCIPATION_ICONS_H
#define EMANCIPATION_ICONS_H

#include <cairo.h>

/* Returns a malloc'd absolute path for an icon name, or NULL. */
char *icon_resolve_path(const char *icon);
/* Loads and caches nothing; caller owns the returned surface. NULL when the
 * icon is missing or not a raster image. */
cairo_surface_t *icon_load_surface(const char *icon, int target_px);

#endif /* EMANCIPATION_ICONS_H */