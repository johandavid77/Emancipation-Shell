#pragma once
#include "render/gl_shared_context.h"
#include "render/render_target.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>

struct renderer_gl {
    struct gl_shared_context *shared;
    EGLContext ctx;
    struct render_target *rt;
};

void renderer_gl_init(struct renderer_gl *r, struct gl_shared_context *shared);
void renderer_gl_cleanup(struct renderer_gl *r);
int renderer_gl_make_current(struct renderer_gl *r, struct render_target *rt);
void renderer_gl_begin(struct renderer_gl *r);
void renderer_gl_end(struct renderer_gl *r, struct render_target *rt);
void renderer_gl_clear(struct renderer_gl *r, float rcol, float g, float b, float a);
void renderer_gl_set_viewport(struct renderer_gl *r, int w, int h);
