#include "core/seat.h"
#include "core/surface_mgr.h"
#include "core/wayland.h"
#include "util/log.h"
#include "cursor-shape-v1-client-protocol.h"
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
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
    double y;
    double scroll_acc;
    struct wl_keyboard *keyboard;
    struct xkb_context *xkb_ctx;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    seat_key_fn key_fn;
    void *key_userdata;
};

static void set_shape(struct seat_state *s, uint32_t shape)
{
    if (!s->shape_dev || s->shape == shape) return; /* skip duplicates like Noctalia */
    wp_cursor_shape_device_v1_set_shape(s->shape_dev, s->enter_serial, shape);
    s->shape = shape;
}

static void update_hover(struct seat_state *s)
{
    bool clickable = surface_mgr_pointer_motion(s->mgr, s->focus, s->x, s->y);
    set_shape(s, clickable ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER
                           : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
}

static void p_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surf,
                    wl_fixed_t x, wl_fixed_t y)
{
    (void)p;
    struct seat_state *s = data;
    s->focus = surf;
    s->enter_serial = serial;
    s->shape = 0; /* the new serial requires re-sending the shape */
    s->x = wl_fixed_to_double(x);
    s->y = wl_fixed_to_double(y);
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
    (void)p; (void)time;
    struct seat_state *s = data;
    if (!s->focus) return;
    s->x = wl_fixed_to_double(x);
    s->y = wl_fixed_to_double(y);
    update_hover(s);
}

static void p_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time,
                     uint32_t button, uint32_t state)
{
    (void)p; (void)serial; (void)time;
    struct seat_state *s = data;
    if (!s->focus || state != WL_POINTER_BUTTON_STATE_PRESSED) return;
    if (button == BTN_LEFT) surface_mgr_pointer_click(s->mgr, s->focus, s->x, s->y);
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

static void release_keyboard(struct seat_state *s)
{
    if (s->keyboard) {
        if (wl_keyboard_get_version(s->keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
            wl_keyboard_release(s->keyboard);
        else wl_keyboard_destroy(s->keyboard);
    }
    s->keyboard = NULL;
    if (s->xkb_state) xkb_state_unref(s->xkb_state);
    s->xkb_state = NULL;
    if (s->xkb_keymap) xkb_keymap_unref(s->xkb_keymap);
    s->xkb_keymap = NULL;
}

static void k_keymap(void *data, struct wl_keyboard *kb, uint32_t format, int32_t fd, uint32_t size)
{
    (void)kb; (void)size;
    struct seat_state *s = data;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        log_err("unsupported keymap format %u", format);
        close(fd);
        return;
    }
    char *map = mmap(NULL, (size_t)size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) {
        log_err("keymap mmap failed");
        return;
    }
    if (s->xkb_state) xkb_state_unref(s->xkb_state);
    if (s->xkb_keymap) xkb_keymap_unref(s->xkb_keymap);
    s->xkb_keymap = xkb_keymap_new_from_string(s->xkb_ctx, map, XKB_KEYMAP_FORMAT_TEXT_V1,
                                              XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, (size_t)size);
    if (!s->xkb_keymap) {
        log_err("xkb_keymap_new_from_string failed");
        return;
    }
    s->xkb_state = xkb_state_new(s->xkb_keymap);
}

static void k_enter(void *data, struct wl_keyboard *kb, uint32_t serial, struct wl_surface *surf,
                    struct wl_array *keys)
{
    (void)kb; (void)serial; (void)surf;
    struct seat_state *s = data;
    /* mark the currently held keys as depressed so a key repeat / modifier that
     * was already down at focus time resolves to the right keysym */
    if (s->xkb_state && keys->size) {
        uint32_t *k = keys->data;
        size_t n = keys->size / sizeof(uint32_t);
        for (size_t i = 0; i < n; i++) xkb_state_update_key(s->xkb_state, k[i] + 8, WL_KEYBOARD_KEY_STATE_PRESSED);
    }
    log_debug("keyboard focus gained");
}

static void k_leave(void *data, struct wl_keyboard *kb, uint32_t serial, struct wl_surface *surf)
{
    (void)kb; (void)serial; (void)surf;
    (void)data;
}

static void k_key(void *data, struct wl_keyboard *kb, uint32_t serial, uint32_t time, uint32_t key,
                  uint32_t state)
{
    (void)kb; (void)serial; (void)time;
    struct seat_state *s = data;
    if (!s->xkb_state) return;
    xkb_state_update_key(s->xkb_state, key + 8,
                         state == WL_KEYBOARD_KEY_STATE_PRESSED ? 1 : 0);
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(s->xkb_state, key + 8);
    if (sym == XKB_KEY_NoSymbol) return;
    uint32_t mods = 0;
    if (xkb_state_mod_name_is_active(s->xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE)) mods |= 1;
    if (xkb_state_mod_name_is_active(s->xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE)) mods |= 2;
    if (xkb_state_mod_name_is_active(s->xkb_state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE)) mods |= 4;
    if (s->key_fn) s->key_fn((uint32_t)sym, mods, s->key_userdata);
}

static void k_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial, uint32_t depressed,
                        uint32_t latched, uint32_t locked, uint32_t group)
{
    (void)kb; (void)serial; (void)depressed; (void)latched; (void)locked; (void)group;
    (void)data;
}

static void k_repeat_info(void *data, struct wl_keyboard *kb, int32_t rate, int32_t delay)
{
    (void)data; (void)kb; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = k_keymap,
    .enter = k_enter,
    .leave = k_leave,
    .key = k_key,
    .modifiers = k_modifiers,
    .repeat_info = k_repeat_info,
};

static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps)
{
    struct seat_state *s = data;
    log_info("seat caps: 0x%x", caps);
    bool has_kb = caps & WL_SEAT_CAPABILITY_KEYBOARD;
    if (has_kb && !s->keyboard) {
        s->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(s->keyboard, &keyboard_listener, s);
        log_info("keyboard available");
    } else if (!has_kb && s->keyboard) {
        release_keyboard(s);
    }
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
    s->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!s->xkb_ctx) log_err("xkb_context_new failed");
    return s;
}

void seat_set_key_handler(struct seat_state *s, seat_key_fn fn, void *userdata)
{
    if (!s) return;
    s->key_fn = fn;
    s->key_userdata = userdata;
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
    release_keyboard(s);
    if (s->xkb_ctx) xkb_context_unref(s->xkb_ctx);
    if (s->seat) {
        if (wl_seat_get_version(s->seat) >= WL_SEAT_RELEASE_SINCE_VERSION) wl_seat_release(s->seat);
        else wl_seat_destroy(s->seat);
    }
    free(s);
}
