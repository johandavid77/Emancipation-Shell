#ifndef EMANCIPATION_CLIPBOARD_H
#define EMANCIPATION_CLIPBOARD_H

#include <time.h>

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
};

struct clipboard *clipboard_create(int max_entries);
void clipboard_destroy(struct clipboard *cb);
void clipboard_add_text(struct clipboard *cb, const char *text);
int clipboard_unpinned_count(const struct clipboard *cb);

#endif /* EMANCIPATION_CLIPBOARD_H */
