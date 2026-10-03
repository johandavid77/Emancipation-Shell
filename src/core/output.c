#include "core/output.h"
#include "core/surface_mgr.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>

static void output_geometry(void *data, struct wl_output *wl_output,
                            int32_t x, int32_t y,
                            int32_t physical_width, int32_t physical_height,
                            int32_t subpixel,
                            const char *make, const char *model,
                            int32_t transform)
{
    (void)wl_output;
    (void)physical_width;
    (void)physical_height;
    (void)subpixel;
    (void)make;
    (void)model;
    (void)transform;
    struct output *out = (struct output *)data;
    out->x = x;
    out->y = y;
}

static void output_mode(void *data, struct wl_output *wl_output,
                        uint32_t flags,
                        int32_t width, int32_t height,
                        int32_t refresh)
{
    (void)wl_output;
    (void)flags;
    (void)refresh;
    struct output *out = (struct output *)data;
    if (width > 0 && height > 0) {
        out->w = width;
        out->h = height;
    }
}

static void output_done(void *data, struct wl_output *wl_output)
{
    (void)wl_output;
    struct output *out = (struct output *)data;
    out->ready = 1;
}

static void output_scale(void *data, struct wl_output *wl_output,
                         int32_t factor)
{
    (void)wl_output;
    struct output *out = (struct output *)data;
    if (factor > 0) {
        out->scale = factor;
    }
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
};

void output_init(struct output *out, uint32_t global_name, struct wl_output *wl)
{
    memset(out, 0, sizeof(*out));
    out->global_name = global_name;
    out->wl = wl;
    out->scale = 1;
    out->w = 0;
    out->h = 0;
    out->x = 0;
    out->y = 0;
    out->ready = 0;
    wl_output_add_listener(wl, &output_listener, out);
}

void output_destroy(struct output *out)
{
    if (!out) {
        return;
    }
    if (out->wl) {
        wl_output_destroy(out->wl);
        out->wl = NULL;
    }
    out->global_name = 0;
}
