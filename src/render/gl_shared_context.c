#include "render/gl_shared_context.h"
#include "util/log.h"
#include <EGL/egl.h>
#include <string.h>
#include <dlfcn.h>

#ifndef EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT 0x3138
#endif
#ifndef EGL_LOSE_CONTEXT_ON_RESET_EXT
#define EGL_LOSE_CONTEXT_ON_RESET_EXT 0x31BF
#endif
#ifndef EGL_GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV
#define EGL_GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV 0x334C
#endif

static const EGLint k_config_attrs[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_NONE
};
static const EGLint k_plain_ctx[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
};

static int has_ext(const char *exts, const char *name)
{
    if (!exts || !name) return 0;
    size_t n = strlen(name);
    const char *p = exts;
    while ((p = strstr(p, name))) {
        if ((p == exts || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0')) return 1;
        p += n;
    }
    return 0;
}

void gl_shared_context_init(struct gl_shared_context *s, struct wl_display *dpy, int create_shared)
{
    if (!s || !dpy) return;
    memset(s, 0, sizeof(*s));
    s->shared_ctx_enabled = create_shared ? 1 : 0;
    s->dpy = eglGetDisplay((EGLNativeDisplayType)dpy);
    if (s->dpy == EGL_NO_DISPLAY) {
        log_err("eglGetDisplay failed");
        return;
    }
    EGLint maj=0,min=0;
    if (!eglInitialize(s->dpy, &maj, &min)) {
        log_err("eglInitialize failed");
        return;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        log_err("eglBindAPI failed");
        return;
    }
    EGLint cnt=0;
    if (!eglChooseConfig(s->dpy, k_config_attrs, &s->cfg, 1, &cnt) || cnt != 1) {
        log_err("eglChooseConfig failed");
        return;
    }
    /* build attrs minimal */
    s->ctx_attrs[0] = EGL_CONTEXT_CLIENT_VERSION; s->ctx_attrs[1] = 2;
    s->ctx_attrs[2] = EGL_NONE;
    if (create_shared) {
        s->root = eglCreateContext(s->dpy, s->cfg, EGL_NO_CONTEXT, s->ctx_attrs);
        if (s->root == EGL_NO_CONTEXT) {
            log_warn("eglCreateContext root failed, trying plain");
            s->root = eglCreateContext(s->dpy, s->cfg, EGL_NO_CONTEXT, k_plain_ctx);
        }
        log_info("EGL initialized with shared context");
    } else {
        log_info("EGL initialized without shared context");
    }
}

void gl_shared_context_cleanup(struct gl_shared_context *s)
{
    if (!s || s->dpy == EGL_NO_DISPLAY) return;
    eglMakeCurrent(s->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (s->root != EGL_NO_CONTEXT) {
        eglDestroyContext(s->dpy, s->root);
        s->root = EGL_NO_CONTEXT;
    }
    eglTerminate(s->dpy);
    s->dpy = EGL_NO_DISPLAY;
    s->cfg = 0;
}

int gl_shared_context_make_current_surfaceless(const struct gl_shared_context *s)
{
    if (!s || s->dpy == EGL_NO_DISPLAY || s->root == EGL_NO_CONTEXT) return 0;
    if (eglGetCurrentDisplay() == s->dpy && eglGetCurrentContext() == s->root) return 1;
    return eglMakeCurrent(s->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, s->root) == EGL_TRUE ? 1 : 0;
}

EGLContext gl_shared_context_create_context(const struct gl_shared_context *s, EGLContext share)
{
    if (!s || s->dpy == EGL_NO_DISPLAY) return EGL_NO_CONTEXT;
    EGLContext ctx = eglCreateContext(s->dpy, s->cfg, share == EGL_NO_CONTEXT ? s->root : share, s->ctx_attrs);
    if (ctx != EGL_NO_CONTEXT) return ctx;
    ctx = eglCreateContext(s->dpy, s->cfg, share == EGL_NO_CONTEXT ? s->root : share, k_plain_ctx);
    return ctx;
}
