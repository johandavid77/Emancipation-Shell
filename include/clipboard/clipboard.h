#ifndef EMANCIPATION_CLIPBOARD_H
#define EMANCIPATION_CLIPBOARD_H

#include <time.h>
#include <stdbool.h>
#include <stdint.h>

struct wayland_ctx;
struct zwlr_data_control_manager_v1;
struct zwlr_data_control_device_v1;
struct zwlr_data_control_offer_v1;

struct clip_item {
    char text[4096];
    char mime[64];
    int pinned;
    time_t ts;
};

struct clipboard {
    struct clip_item *items;
    int count;
    int capacity;
    int max_entries;

    /* Wayland clipboard integration via wlr-data-control */
    struct wayland_ctx *ctx;
    struct zwlr_data_control_device_v1 *device;
    struct zwlr_data_control_offer_v1 *offer;
    char mimes[16][128];
    int n_mimes;
    uint32_t serial;
};

struct clipboard *clipboard_create(int max_entries);
void clipboard_destroy(struct clipboard *cb);
void clipboard_add_text(struct clipboard *cb, const char *text);
int clipboard_unpinned_count(const struct clipboard *cb);

/* Bind a wlr-data-control device (needs a bound wl_seat). Call after the
 * initial registry roundtrip. Returns true when the selection is tracked. */
bool clipboard_connect(struct clipboard *cb, struct wayland_ctx *ctx);

/* Put `text` on the system clipboard (serves it to other clients). */
bool clipboard_set_text(struct clipboard *cb, const char *text);

#endif /* EMANCIPATION_CLIPBOARD_H */
