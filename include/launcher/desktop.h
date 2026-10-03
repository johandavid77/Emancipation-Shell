#ifndef EMANCIPATION_DESKTOP_H
#define EMANCIPATION_DESKTOP_H

#include <stdbool.h>

struct desktop_entry {
    char name[256];
    char generic_name[256];
    char exec[512];
    char icon[256];
    char categories[512];
    char keywords[512];
    bool no_display;
    bool hidden;
};

struct desktop_db {
    struct desktop_entry *entries;
    int count;
    int capacity;
};

void desktop_db_init(struct desktop_db *db);
void desktop_db_free(struct desktop_db *db);
bool desktop_db_add(struct desktop_db *db, const struct desktop_entry *e);
bool desktop_db_load_from_dir(struct desktop_db *db, const char *dir);
bool desktop_db_load_system(struct desktop_db *db);
int desktop_db_search_fuzzy(const struct desktop_db *db, const char *query, int *results, int max_results);

#endif /* EMANCIPATION_DESKTOP_H */
