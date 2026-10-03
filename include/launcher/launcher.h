#ifndef EMANCIPATION_LAUNCHER_H
#define EMANCIPATION_LAUNCHER_H

#include <stdbool.h>

struct launcher;
struct wayland_ctx;

struct launcher *launcher_create(struct wayland_ctx *ctx);
void launcher_destroy(struct launcher *l);
void launcher_show(struct launcher *l);
void launcher_hide(struct launcher *l);
void launcher_toggle(struct launcher *l);
bool launcher_is_visible(struct launcher *l);
void launcher_set_query(struct launcher *l, const char *q);
void launcher_exec_selected(struct launcher *l);

#endif /* EMANCIPATION_LAUNCHER_H */
