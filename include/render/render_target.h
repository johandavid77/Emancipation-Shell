#pragma once
#include <wayland-client.h>
#include <EGL/egl.h>
#include <wayland-egl-core.h>
#include <GLES2/gl2.h>

struct render_target {
    struct wl_egl_window *win;
    EGLSurface surf;
    int w, h;
};

void render_target_create(struct render_target *rt, struct wl_surface *wsurf, EGLDisplay dpy, EGLConfig cfg, int w, int h);
void render_target_resize(struct render_target *rt, EGLDisplay dpy, int w, int h);
void render_target_destroy(struct render_target *rt, EGLDisplay dpy);
