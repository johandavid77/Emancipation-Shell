#include "render/render_target.h"
#include <stdlib.h>

void render_target_create(struct render_target *rt, struct wl_surface *wsurf, EGLDisplay dpy, EGLConfig cfg, int w, int h)
{
    if (!rt) return;
    rt->w = w > 0 ? w : 1;
    rt->h = h > 0 ? h : 1;
    rt->win = wl_egl_window_create(wsurf, rt->w, rt->h);
    rt->surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)rt->win, NULL);
}

void render_target_resize(struct render_target *rt, EGLDisplay dpy, int w, int h)
{
    if (!rt || !rt->win) return;
    rt->w = w > 0 ? w : rt->w;
    rt->h = h > 0 ? h : rt->h;
    wl_egl_window_resize(rt->win, rt->w, rt->h, 0, 0);
}

void render_target_destroy(struct render_target *rt, EGLDisplay dpy)
{
    if (!rt) return;
    if (rt->surf != EGL_NO_SURFACE && dpy != EGL_NO_DISPLAY) {
        eglDestroySurface(dpy, rt->surf);
        rt->surf = EGL_NO_SURFACE;
    }
    if (rt->win) {
        wl_egl_window_destroy(rt->win);
        rt->win = NULL;
    }
    rt->w = rt->h = 0;
}
