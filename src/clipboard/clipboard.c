#define _GNU_SOURCE
#include "clipboard/clipboard.h"
#include "core/wayland.h"
#include "core/seat.h"
#include "util/log.h"
#include "wlr-data-control-unstable-v1-client-protocol.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <wayland-client.h>

struct clipboard *clipboard_create(int max_entries)
{
    struct clipboard *cb = calloc(1, sizeof(*cb));
    if (!cb) return NULL;
    cb->max_entries = max_entries > 0 ? max_entries : 100;
    log_debug("clipboard created (max=%d)", cb->max_entries);
    return cb;
}

void clipboard_destroy(struct clipboard *cb)
{
    if (!cb) return;
    if (cb->offer) zwlr_data_control_offer_v1_destroy(cb->offer);
    if (cb->device) zwlr_data_control_device_v1_destroy(cb->device);
    free(cb->items);
    free(cb);
}

void clipboard_add_text(struct clipboard *cb, const char *text)
{
    if (!cb || !text || !*text) return;
    if (cb->count > 0 && strcmp(cb->items[cb->count - 1].text, text) == 0) return;
    if (cb->count >= cb->capacity) {
        int nc = cb->capacity == 0 ? 16 : cb->capacity * 2;
        struct clip_item *ni = realloc(cb->items, nc * sizeof(*ni));
        if (!ni) return;
        cb->items = ni;
        cb->capacity = nc;
    }
    strncpy(cb->items[cb->count].text, text, sizeof(cb->items[cb->count].text)-1);
    cb->items[cb->count].text[sizeof(cb->items[cb->count].text)-1] = '\0';
    strncpy(cb->items[cb->count].mime, "text/plain", sizeof(cb->items[cb->count].mime)-1);
    cb->items[cb->count].mime[sizeof(cb->items[cb->count].mime)-1] = '\0';
    cb->items[cb->count].pinned = 0;
    cb->items[cb->count].ts = time(NULL);
    cb->count++;
    if (cb->count > cb->max_entries) {
        int unpinned = 0;
        for (int i = 0; i < cb->count; i++) {
            if (!cb->items[i].pinned) unpinned++;
        }
        if (unpinned > 0) {
            memmove(cb->items, cb->items + 1, (cb->count - 1) * sizeof(*cb->items));
            cb->count--;
        }
    }
    log_debug("clipboard added text (%d entries)", cb->count);
}

int clipboard_unpinned_count(const struct clipboard *cb)
{
    if (!cb) return 0;
    int c = 0;
    for (int i = 0; i < cb->count; i++) {
        if (!cb->items[i].pinned) c++;
    }
    return c;
}

/* ---- wlr-data-control: capture incoming selections ------------------------ */

static bool is_text_mime(const char *m)
{
    return strcmp(m, "text/plain;charset=utf-8") == 0 ||
           strcmp(m, "text/plain;charset=UTF-8") == 0 ||
           strcmp(m, "text/plain") == 0 ||
           strcmp(m, "UTF8_STRING") == 0 ||
           strcmp(m, "STRING") == 0 || strcmp(m, "TEXT") == 0;
}

static const char *pick_text_mime(struct clipboard *cb)
{
    static const char *pref[] = {
        "text/plain;charset=utf-8", "UTF8_STRING", "text/plain;charset=UTF-8",
        "text/plain", "TEXT", "STRING", NULL
    };
    for (int i = 0; pref[i]; i++) {
        for (int j = 0; j < cb->n_mimes; j++) {
            if (strcmp(pref[i], cb->mimes[j]) == 0) return pref[i];
        }
    }
    return NULL;
}

static void offer_mime(void *data, struct zwlr_data_control_offer_v1 *offer, const char *mime)
{
    (void)offer;
    struct clipboard *cb = data;
    if (!mime || cb->n_mimes >= 16) return;
    if (!is_text_mime(mime)) return;
    for (int i = 0; i < cb->n_mimes; i++)
        if (strcmp(cb->mimes[i], mime) == 0) return;
    snprintf(cb->mimes[cb->n_mimes], sizeof(cb->mimes[0]), "%s", mime);
    cb->n_mimes++;
}

static const struct zwlr_data_control_offer_v1_listener offer_listener = {
    .offer = offer_mime,
};

static void read_selection(struct clipboard *cb)
{
    if (!cb || !cb->offer || !cb->ctx) return;
    const char *mime = pick_text_mime(cb);
    if (!mime) {
        zwlr_data_control_offer_v1_destroy(cb->offer);
        cb->offer = NULL;
        return;
    }
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) < 0) {
        zwlr_data_control_offer_v1_destroy(cb->offer);
        cb->offer = NULL;
        return;
    }
    zwlr_data_control_offer_v1_receive(cb->offer, mime, fds[1]);
    close(fds[1]);
    wl_display_flush(cb->ctx->display);

    char buf[4096];
    size_t total = 0;
    struct pollfd pfd = { .fd = fds[0], .events = POLLIN };
    for (;;) {
        int pr = poll(&pfd, 1, 500);
        if (pr <= 0) break;
        ssize_t n = read(fds[0], buf + total, sizeof(buf) - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
        if (total >= sizeof(buf) - 1) break;
    }
    close(fds[0]);
    if (total > 0) {
        buf[total] = '\0';
        clipboard_add_text(cb, buf);
    }
    zwlr_data_control_offer_v1_destroy(cb->offer);
    cb->offer = NULL;
}

static void dc_data_offer(void *data, struct zwlr_data_control_device_v1 *dd,
                          struct zwlr_data_control_offer_v1 *offer)
{
    (void)dd;
    struct clipboard *cb = data;
    if (cb->offer) zwlr_data_control_offer_v1_destroy(cb->offer);
    cb->offer = offer;
    cb->n_mimes = 0;
    zwlr_data_control_offer_v1_add_listener(offer, &offer_listener, cb);
}

static void dc_selection(void *data, struct zwlr_data_control_device_v1 *dd,
                         struct zwlr_data_control_offer_v1 *offer)
{
    (void)dd;
    struct clipboard *cb = data;
    if (!offer) {
        if (cb->offer) { zwlr_data_control_offer_v1_destroy(cb->offer); cb->offer = NULL; }
        cb->n_mimes = 0;
        return;
    }
    if (cb->offer != offer) {
        if (cb->offer) zwlr_data_control_offer_v1_destroy(cb->offer);
        cb->offer = offer;
        zwlr_data_control_offer_v1_add_listener(offer, &offer_listener, cb);
    }
    read_selection(cb);
}

static void dc_finished(void *data, struct zwlr_data_control_device_v1 *dd)
{
    (void)dd;
    struct clipboard *cb = data;
    log_warn("clipboard: data-control device finished");
    cb->device = NULL;
}

static void dc_primary_selection(void *data, struct zwlr_data_control_device_v1 *dd,
                                 struct zwlr_data_control_offer_v1 *offer)
{
    (void)data; (void)dd;
    if (offer) zwlr_data_control_offer_v1_destroy(offer);
}

static const struct zwlr_data_control_device_v1_listener device_listener = {
    .data_offer = dc_data_offer,
    .selection = dc_selection,
    .finished = dc_finished,
    .primary_selection = dc_primary_selection,
};

bool clipboard_connect(struct clipboard *cb, struct wayland_ctx *ctx)
{
    if (!cb || !ctx) return false;
    cb->ctx = ctx;
    if (!ctx->data_control_mgr) {
        log_warn("clipboard: no zwlr_data_control_manager_v1; history disabled");
        return false;
    }
    struct wl_seat *seat = seat_wl_seat(ctx->seat);
    if (!seat) {
        log_warn("clipboard: no wl_seat; history disabled");
        return false;
    }
    cb->device = zwlr_data_control_manager_v1_get_data_device(ctx->data_control_mgr, seat);
    if (!cb->device) return false;
    zwlr_data_control_device_v1_add_listener(cb->device, &device_listener, cb);
    log_info("clipboard: wlr-data-control ready");
    return true;
}

/* ---- serve our own clipboard (paste back a history entry) ----------------- */

struct clip_source {
    struct clipboard *cb;
    struct zwlr_data_control_source_v1 *src;
    char *text;
};

static struct clip_source g_source;

static void src_send(void *data, struct zwlr_data_control_source_v1 *src, const char *mime, int32_t fd)
{
    (void)src;
    struct clip_source *s = data;
    if (s->text) {
        size_t len = strlen(s->text);
        ssize_t w = write(fd, s->text, len);
        (void)w;
    }
    (void)mime;
    close(fd);
}

static void src_cancelled(void *data, struct zwlr_data_control_source_v1 *src)
{
    struct clip_source *s = data;
    zwlr_data_control_source_v1_destroy(src);
    s->src = NULL;
    free(s->text);
    s->text = NULL;
}

static const struct zwlr_data_control_source_v1_listener source_listener = {
    .send = src_send,
    .cancelled = src_cancelled,
};

bool clipboard_set_text(struct clipboard *cb, const char *text)
{
    if (!cb || !cb->ctx || !cb->ctx->data_control_mgr || !text) return false;
    if (g_source.src) {
        zwlr_data_control_source_v1_destroy(g_source.src);
        g_source.src = NULL;
        free(g_source.text);
        g_source.text = NULL;
    }
    g_source.cb = cb;
    g_source.text = strdup(text);
    if (!g_source.text) return false;
    g_source.src = zwlr_data_control_manager_v1_create_data_source(cb->ctx->data_control_mgr);
    if (!g_source.src) { free(g_source.text); g_source.text = NULL; return false; }
    zwlr_data_control_source_v1_add_listener(g_source.src, &source_listener, &g_source);
    zwlr_data_control_source_v1_offer(g_source.src, "text/plain;charset=utf-8");
    zwlr_data_control_source_v1_offer(g_source.src, "text/plain");
    zwlr_data_control_source_v1_offer(g_source.src, "UTF8_STRING");
    zwlr_data_control_device_v1_set_selection(cb->device, g_source.src);
    wl_display_flush(cb->ctx->display);
    log_info("clipboard: set selection");
    return true;
}
