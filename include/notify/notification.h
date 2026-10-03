#ifndef EMANCIPATION_NOTIFICATION_H
#define EMANCIPATION_NOTIFICATION_H

#include <time.h>

struct notification {
    unsigned int id;
    char app_name[128];
    char summary[256];
    char body[512];
    char icon[256];
    time_t timestamp;
    int urgency;
    int timeout;
    int read;
};

struct notification_store {
    struct notification *items;
    int count;
    int capacity;
    unsigned int next_id;
};

void notify_store_init(struct notification_store *s);
void notify_store_free(struct notification_store *s);
unsigned int notify_store_add(struct notification_store *s, const struct notification *n);
int notify_store_unread_count(const struct notification_store *s);

#endif /* EMANCIPATION_NOTIFICATION_H */
