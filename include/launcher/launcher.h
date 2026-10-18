#ifndef EMANCIPATION_LAUNCHER_H
#define EMANCIPATION_LAUNCHER_H

#include <stdbool.h>
#include <cairo.h>
#include <stddef.h>

struct launcher;
struct wayland_ctx;
struct clipboard;
struct wallpaper;
struct config;

enum launcher_res_type {
    LR_APP = 0,
    LR_CALC,
    LR_EMOJI,
    LR_WALLPAPER,
    LR_GENERIC
};

struct launcher_result {
    char name[256];   /* primary text (title/result) */
    char desc[256];   /* secondary text */
    char icon[256];   /* icon name/path or glyph */
    enum launcher_res_type type;
    int app_index;    /* desktop entry index for LR_APP, else -1 */
    void *data;       /* provider-specific payload (owned by launcher) */
};

struct launcher_provider {
    const char *name;
    /* Fill up to `max` results into `out`; return the number produced. */
    int (*match)(struct launcher *l, const char *q, struct launcher_result *out, int max);
    /* Run the selected result. */
    void (*activate)(struct launcher *l, struct launcher_result *r);
};

struct launcher *launcher_create(struct wayland_ctx *ctx);
void launcher_destroy(struct launcher *l);
void launcher_show(struct launcher *l);
void launcher_hide(struct launcher *l);
void launcher_toggle(struct launcher *l);
bool launcher_is_visible(struct launcher *l);
void launcher_set_query(struct launcher *l, const char *q);
void launcher_exec_selected(struct launcher *l);

void launcher_register_provider(struct launcher *l, struct launcher_provider *p);
void launcher_register_default_providers(struct launcher *l);

/* External services the providers may use (clipboard, wallpaper). */
void launcher_set_clipboard(struct launcher *l, struct clipboard *clip);
void launcher_set_wallpaper(struct launcher *l, struct wallpaper *wp);
void launcher_set_config(struct launcher *l, const struct config *cfg);

void launcher_get_query(struct launcher *l, char *out, size_t outsz);
void launcher_get_results_list(struct launcher *l, struct launcher_result *out, int *count_out, int maxn);
cairo_surface_t *launcher_icon_for(struct launcher *l, int result_idx, int px);
void launcher_activate_result(struct launcher *l, struct launcher_result *r);

#endif /* EMANCIPATION_LAUNCHER_H */
