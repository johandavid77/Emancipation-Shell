#ifndef EMANCIPATION_CONFIG_H
#define EMANCIPATION_CONFIG_H

#include <stdbool.h>

#define BAR_MAX_MODULES 20
#define BAR_MODULE_NAME 96
#define DOCK_MAX_PINNED 16
#define DOCK_PINNED_NAME 128

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
    bool show_weather; /* show the weather widget in the bar */
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
    struct color_rgba panel_background; /* overlay panels: niri-like neutral */
    struct color_rgba foreground;      /* on_surface */
    struct color_rgba primary;
    struct color_rgba on_primary;
    struct color_rgba secondary;
    struct color_rgba on_secondary;
    struct color_rgba error;
    struct color_rgba on_error;
    struct color_rgba surface_variant;
    struct color_rgba on_surface_variant;
    bool dark;   /* true = dark palette, false = light palette */
    bool night;  /* night-light: warm-shift the resolved palette */
};

struct font_config {
    char family[256];
    int size;
};

enum wp_fill_mode {
    WP_FILL_CROP,
    WP_FILL_FIT,
    WP_FILL_STRETCH,
    WP_FILL_CENTER,
    WP_FILL_REPEAT,
    WP_FILL_SPAN,
    WP_FILL_MAX
};

struct wallpaper_config {
    char path[512];
    char dir[512];
    enum wp_fill_mode fill;
    bool enabled;
    int interval; /* minutes, 0 = disabled */
    bool randomize;
    int transition; /* crossfade duration in ms, 0 = instant */
};

struct idle_config {
    bool enabled;
    int lock_timeout;       /* seconds of inactivity before locking, 0 = never */
    int screen_off_timeout; /* seconds before turning monitors off, 0 = never */
};

/* Master switch for the screen lock. When disabled, locking is refused from
 * every path (idle timer, manual lock, shortcuts, control center). */
struct lock_config {
    bool enabled;
};

struct dock_config {
    bool enabled;
    bool autohide;
    int height;     /* total dock bar height (icon + padding) */
    int icon_size;  /* icon edge, logical px */
    char pinned[DOCK_MAX_PINNED][DOCK_PINNED_NAME]; /* desktop entry ids */
    int n_pinned;
};

struct widgets_config {
    bool enabled;
    bool clock;     /* big clock on the desktop */
    bool sysmon;    /* CPU/RAM monitor on the desktop */
    int position;   /* 0 = top-left, 1 = top-right */
    int width;      /* widget card width */
};

struct config {
    struct bar_config bar;
    struct theme_config theme;
    struct font_config font;
    struct wallpaper_config wallpaper;
    struct idle_config idle;
    struct lock_config lock;
    struct dock_config dock;
    struct widgets_config widgets;
};

void config_defaults(struct config *cfg);
bool config_equal(const struct config *a, const struct config *b);
/* Persist bar.show_weather to the config file (atomic replace, hot-reloaded). */
bool config_write_bar_show_weather(bool on);
/* Persist wallpaper.dir to the config file (atomic replace, hot-reloaded). */
bool config_write_wallpaper_dir(const char *dir);
/* Persist wallpaper.path to the config file (atomic replace, hot-reloaded). */
bool config_write_wallpaper_path(const char *path);
/* Persist the screen-lock master switch (atomic replace, hot-reloaded). */
bool config_write_lock_enabled(bool on);
/* Persist the theme dark/night variant (atomic replace, hot-reloaded). */
bool config_write_theme_dark(bool on);
bool config_write_theme_night(bool on);
/* Overwrite the palette roles with the builtin dark or light palette. */
void theme_apply_palette(struct theme_config *th, bool dark);
/* Warm-shift an already resolved palette (night light). */
void theme_apply_night(struct theme_config *th);
/* "#rrggbb" or "#rrggbbaa" */
bool color_from_hex(const char *hex, struct color_rgba *out);
const char *wp_fill_to_str(enum wp_fill_mode m);
enum wp_fill_mode wp_fill_from_str(const char *s);

#endif /* EMANCIPATION_CONFIG_H */
