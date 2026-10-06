#include "core/seat.h"
#include "core/surface_mgr.h"
#include "core/wayland.h"
#include "util/log.h"
#include "cursor-shape-v1-client-protocol.h"
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>

/* One wheel notch reports ~15 units (10 on some compositors). */
#define SCROLL_STEP 10.0

struct seat_state {
    struct wayland_ctx *ctx;
    struct surface_mgr *mgr;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wp_cursor_shape_device_v1 *shape_dev;
    struct wl_surface *focus;
    uint32_t enter_serial;
    uint32_t shape; /* last shape sent, 0 = none yet */
    double x;
    double scroll_acc;
};

static void set_shape(struct seat_state *s, uint32_t shape)
{
    if (!s->shape_dev || s->shape == shape) return; /* skip duplicates like Noctalia */
    wp_cursor_shape_device_v1_set_shape(s->shape_dev, s->enter_serial, shape);
    s->shape = shape;
}

static void update_hover(struct seat_state *s)
{
    bool clickable = surface_mgr_pointer_motion(s->mgr, s->focus, s->x);
    set_shape(s, clickable ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER
                           : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
}

static void p_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surf,
                    wl_fixed_t x, wl_fixed_t y)
{
    (void)p; (void)y;
    struct seat_state *s = data;
    s->focus = surf;
    s->enter_serial = serial;
    s->shape = 0; /* the new serial requires re-sending the shape */
    s->x = wl_fixed_to_double(x);
    s->scroll_acc = 0;
    update_hover(s);
}

static void p_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surf)
{
    (void)p; (void)serial;
    struct seat_state *s = data;
    surface_mgr_pointer_leave(s->mgr, surf);
    s->focus = NULL;
}

static void p_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    (void)p; (void)time; (void)y;
    struct seat_state *s = data;
    if (!s->focus) return;
    s->x = wl_fixed_to_double(x);
    update_hover(s);
}

static void p_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time,
                     uint32_t button, uint32_t state)
{
    (void)p; (void)serial; (void)time;
    struct seat_state *s = data;
    if (!s->focus || state != WL_POINTER_BUTTON_STATE_PRESSED) return;
    if (button == BTN_LEFT) surface_mgr_pointer_click(s->mgr, s->focus, s->x);
}

static void p_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t value)
{
    (void)p; (void)time;
    struct seat_state *s = data;
    if (!s->focus || axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
    s->scroll_acc += wl_fixed_to_double(value);
    while (s->scroll_acc >= SCROLL_STEP) {
        surface_mgr_pointer_scroll(s->mgr, s->focus, +1); /* down = next */
        s->scroll_acc -= SCROLL_STEP;
    }
    while (s->scroll_acc <= -SCROLL_STEP) {
        surface_mgr_pointer_scroll(s->mgr, s->focus, -1); /* up = prev */
        s->scroll_acc += SCROLL_STEP;
    }
}

static void p_frame(void *data, struct wl_pointer *p) { (void)data; (void)p; }
static void p_axis_source(void *data, struct wl_pointer *p, uint32_t src) { (void)data; (void)p; (void)src; }
static void p_axis_stop(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis)
{
    (void)p; (void)time;
    struct seat_state *s = data;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) s->scroll_acc = 0;
}
static void p_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis, int32_t d)
{
    (void)data; (void)p; (void)axis; (void)d;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = p_enter,
    .leave = p_leave,
    .motion = p_motion,
    .button = p_button,
    .axis = p_axis,
    .frame = p_frame,
    .axis_source = p_axis_source,
    .axis_stop = p_axis_stop,
    .axis_discrete = p_axis_discrete,
};

static void release_pointer(struct seat_state *s)
{
    if (s->shape_dev) wp_cursor_shape_device_v1_destroy(s->shape_dev);
    s->shape_dev = NULL;
    if (s->pointer) {
        if (wl_pointer_get_version(s->pointer) >= WL_POINTER_RELEASE_SINCE_VERSION) wl_pointer_release(s->pointer);
        else wl_pointer_destroy(s->pointer);
    }
    s->pointer = NULL;
    s->focus = NULL;
}

static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps)
{
    struct seat_state *s = data;
    bool has = caps & WL_SEAT_CAPABILITY_POINTER;
    if (has && !s->pointer) {
        s->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(s->pointer, &pointer_listener, s);
        if (s->ctx->cursor_shape_mgr)
            s->shape_dev = wp_cursor_shape_manager_v1_get_pointer(s->ctx->cursor_shape_mgr, s->pointer);
        log_info("pointer available%s", s->shape_dev ? " (cursor-shape)" : "");
    } else if (!has && s->pointer) {
        release_pointer(s);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_caps,
    .name = seat_name,
};

struct seat_state *seat_create(struct wayland_ctx *ctx, struct surface_mgr *mgr)
{
    struct seat_state *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->ctx = ctx;
    s->mgr = mgr;
    return s;
}

void seat_attach(struct seat_state *s, struct wl_seat *wl_seat)
{
    if (!s || s->seat) { /* only the first seat is used */
        wl_seat_destroy(wl_seat);
        return;
    }
    s->seat = wl_seat;
    wl_seat_add_listener(wl_seat, &seat_listener, s);
}

void seat_destroy(struct seat_state *s)
{
    if (!s) return;
    release_pointer(s);
    if (s->seat) {
        if (wl_seat_get_version(s->seat) >= WL_SEAT_RELEASE_SINCE_VERSION) wl_seat_release(s->seat);
        else wl_seat_destroy(s->seat);
    }
    free(s);
}
