#ifndef EMANCIPATION_RENDERER_H
#define EMANCIPATION_RENDERER_H

#include <stdbool.h>

struct renderer;
struct wayland_ctx;

struct renderer *renderer_create(struct wayland_ctx *ctx);
void renderer_destroy(struct renderer *r);
void renderer_begin_frame(struct renderer *r);
void renderer_end_frame(struct renderer *r);
void renderer_set_scale(struct renderer *r, double scale);
double renderer_get_scale(struct renderer *r);

#endif /* EMANCIPATION_RENDERER_H */
