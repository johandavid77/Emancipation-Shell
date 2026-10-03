#include "notify/notification.h"
#include <stdlib.h>
#include <string.h>

void notify_store_init(struct notification_store *s)
{
    if (!s) return;
    s->items = NULL;
    s->count = 0;
    s->capacity = 0;
    s->next_id = 1;
}

void notify_store_free(struct notification_store *s)
{
    if (!s) return;
    free(s->items);
    s->items = NULL;
    s->count = s->capacity = 0;
}

unsigned int notify_store_add(struct notification_store *s, const struct notification *n)
{
    if (!s || !n) return 0;
    if (s->count >= s->capacity) {
        int newcap = s->capacity == 0 ? 32 : s->capacity * 2;
        struct notification *ni = realloc(s->items, newcap * sizeof(*ni));
        if (!ni) return 0;
        s->items = ni;
        s->capacity = newcap;
    }
    struct notification *dst = &s->items[s->count++];
    *dst = *n;
    dst->id = s->next_id++;
    dst->timestamp = time(NULL);
    dst->read = 0;
    return dst->id;
}

int notify_store_unread_count(const struct notification_store *s)
{
    if (!s) return 0;
    int c = 0;
    for (int i = 0; i < s->count; i++) {
        if (!s->items[i].read) c++;
    }
    return c;
}
