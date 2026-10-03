#include "clipboard/clipboard.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>

struct clipboard *clipboard_create(int max_entries)
{
    struct clipboard *cb = calloc(1, sizeof(*cb));
    if (!cb) return NULL;
    cb->max_entries = max_entries > 0 ? max_entries : 100;
    cb->items = NULL;
    cb->count = 0;
    cb->capacity = 0;
    log_debug("clipboard created (max=%d)", cb->max_entries);
    return cb;
}

void clipboard_destroy(struct clipboard *cb)
{
    if (!cb) return;
    free(cb->items);
    free(cb);
}

void clipboard_add_text(struct clipboard *cb, const char *text)
{
    if (!cb || !text || !*text) return;
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
    log_debug("clipboard added text");
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
