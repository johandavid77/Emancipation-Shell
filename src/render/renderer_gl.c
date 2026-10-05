#include "render/renderer_gl.h"
#include "util/log.h"

void renderer_gl_init(struct renderer_gl *r, struct gl_shared_context *shared)
{
    if (!r) return;
    r->shared = shared;
    r->ctx = EGL_NO_CONTEXT;
    r->rt = NULL;
    if (shared) {
        r->ctx = gl_shared_context_create_context(shared, shared->root);
        if (r->ctx == EGL_NO_CONTEXT) {
            log_err("failed to create render context");
        }
    }
}

void renderer_gl_cleanup(struct renderer_gl *r)
{
    if (!r) return;
    if (r->shared && r->ctx != EGL_NO_CONTEXT && r->shared->dpy != EGL_NO_DISPLAY) {
        eglDestroyContext(r->shared->dpy, r->ctx);
        r->ctx = EGL_NO_CONTEXT;
    }
    r->rt = NULL;
}

int renderer_gl_make_current(struct renderer_gl *r, struct render_target *rt)
{
    if (!r || !r->shared || r->ctx == EGL_NO_CONTEXT || !rt || rt->surf == EGL_NO_SURFACE) return 0;
    if (eglGetCurrentDisplay() == r->shared->dpy && eglGetCurrentContext() == r->ctx && eglGetCurrentSurface(EGL_DRAW) == rt->surf) {
        return 1;
    }
    return eglMakeCurrent(r->shared->dpy, rt->surf, rt->surf, r->ctx) == EGL_TRUE ? 1 : 0;
}

void renderer_gl_begin(struct renderer_gl *r)
{
    (void)r;
    glViewport(0, 0, 0, 0);
}

void renderer_gl_set_viewport(struct renderer_gl *r, int w, int h)
{
    (void)r;
    if (w < 0) w = 0; if (h < 0) h = 0;
    glViewport(0, 0, w, h);
}


void renderer_gl_end(struct renderer_gl *r, struct render_target *rt)
{
    if (!r || !rt) return;
    eglSwapBuffers(r->shared ? r->shared->dpy : EGL_NO_DISPLAY, rt->surf);
}

void renderer_gl_clear(struct renderer_gl *r, float rc, float g, float b, float a)
{
    (void)r;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(rc,g,b,a);
    glClear(GL_COLOR_BUFFER_BIT);
}
