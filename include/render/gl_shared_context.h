#pragma once
#include <wayland-client.h>
#include <EGL/egl.h>

struct gl_shared_context {
    EGLDisplay dpy;
    EGLConfig cfg;
    EGLContext root;
    EGLint ctx_attrs[16];
    int shared_ctx_enabled;
    int reset_notify;
    int video_mem_purge;
};

void gl_shared_context_init(struct gl_shared_context *s, struct wl_display *dpy, int create_shared);
void gl_shared_context_cleanup(struct gl_shared_context *s);
int gl_shared_context_make_current_surfaceless(const struct gl_shared_context *s);
EGLContext gl_shared_context_create_context(const struct gl_shared_context *s, EGLContext share);
