#include "launcher/desktop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

static int fuzzy_score(const char *text, const char *query)
{
    if (!query || !*query) return 0;
    if (!text || !*text) return -1;
    const char *t = text;
    const char *q = query;
    int score = 0;
    int consecutive = 0;
    while (*q) {
        char qc = tolower((unsigned char)*q);
        bool found = false;
        while (*t) {
            char tc = tolower((unsigned char)*t);
            if (tc == qc) {
                score += 10 + consecutive * 5;
                consecutive++;
                t++;
                found = true;
                break;
            } else {
                consecutive = 0;
                t++;
            }
        }
        if (!found) return -1;
        q++;
    }
    return score;
}

static void entry_clear(struct desktop_entry *e)
{
    memset(e, 0, sizeof(*e));
}

void desktop_db_init(struct desktop_db *db)
{
    if (!db) return;
    db->entries = NULL;
    db->count = 0;
    db->capacity = 0;
}

void desktop_db_free(struct desktop_db *db)
{
    if (!db) return;
    free(db->entries);
    db->entries = NULL;
    db->count = db->capacity = 0;
}

bool desktop_db_add(struct desktop_db *db, const struct desktop_entry *e)
{
    if (!db || !e) return false;
    if (db->count >= db->capacity) {
        int newcap = db->capacity == 0 ? 64 : db->capacity * 2;
        struct desktop_entry *ne = realloc(db->entries, newcap * sizeof(*ne));
        if (!ne) return false;
        db->entries = ne;
        db->capacity = newcap;
    }
    db->entries[db->count++] = *e;
    return true;
}

static void parse_desktop_line(char *line, struct desktop_entry *e)
{
    char *eq = strchr(line, '=');
    if (!eq) return;
    *eq = '\0';
    char *key = trim(line);
    char *val = trim(eq + 1);
    if (strcmp(key, "Name") == 0) {
        strncpy(e->name, val, sizeof(e->name)-1);
    } else if (strcmp(key, "GenericName") == 0) {
        strncpy(e->generic_name, val, sizeof(e->generic_name)-1);
    } else if (strcmp(key, "Exec") == 0) {
        char buf[512];
        int j = 0;
        for (int i = 0; val[i] && j < (int)sizeof(buf)-1; i++) {
            if (val[i] == '%' && val[i+1]) {
                char c = val[i+1];
                if (c=='U'||c=='u'||c=='F'||c=='f'||c=='i'||c=='c'||c=='k'||c=='d'||c=='D'||c=='n'||c=='N'||c=='v'||c=='m'||c=='t'||c=='x') {
                    i++; continue;
                }
            }
            buf[j++] = val[i];
        }
        buf[j] = '\0';
        strncpy(e->exec, trim(buf), sizeof(e->exec)-1);
    } else if (strcmp(key, "Icon") == 0) {
        strncpy(e->icon, val, sizeof(e->icon)-1);
    } else if (strcmp(key, "Categories") == 0) {
        strncpy(e->categories, val, sizeof(e->categories)-1);
    } else if (strcmp(key, "Keywords") == 0) {
        strncpy(e->keywords, val, sizeof(e->keywords)-1);
    } else if (strcmp(key, "NoDisplay") == 0) {
        e->no_display = (strcasecmp(val, "true") == 0);
    } else if (strcmp(key, "Hidden") == 0) {
        e->hidden = (strcasecmp(val, "true") == 0);
    }
}

static bool load_desktop_file(const char *path, struct desktop_entry *e)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    entry_clear(e);
    char line[1024];
    bool in_desktop = false;
    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (*p == '#' || *p == '\0') continue;
        if (*p == '[') {
            in_desktop = (strncmp(p, "[Desktop Entry]", 15) == 0);
            continue;
        }
        if (in_desktop) {
            parse_desktop_line(p, e);
        }
    }
    fclose(f);
    return (e->name[0] != '\0' && e->exec[0] != '\0');
}

static bool is_desktop_file(const char *name)
{
    const char *ext = strrchr(name, '.');
    return ext && strcmp(ext, ".desktop") == 0;
}

bool desktop_db_load_from_dir(struct desktop_db *db, const char *dir)
{
    if (!db || !dir) return false;
    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (!is_desktop_file(de->d_name)) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;
        struct desktop_entry e;
        if (load_desktop_file(path, &e)) {
            if (!e.hidden && !e.no_display) {
                desktop_db_add(db, &e);
            }
        }
    }
    closedir(d);
    return true;
}

bool desktop_db_load_system(struct desktop_db *db)
{
    if (!db) return false;
    const char *home = getenv("HOME");
    char path[1024];
    if (home) {
        snprintf(path, sizeof(path), "%s/.local/share/applications", home);
        desktop_db_load_from_dir(db, path);
    }
    desktop_db_load_from_dir(db, "/usr/share/applications");
    desktop_db_load_from_dir(db, "/usr/local/share/applications");
    return true;
}

int desktop_db_search_fuzzy(const struct desktop_db *db, const char *query, int *results, int max_results)
{
    if (!db || !query || !results || max_results <= 0) return 0;
    typedef struct { int idx; int score; } item_t;
    item_t tmp[4096];
    int ntmp = 0;
    for (int i = 0; i < db->count && ntmp < 4096; i++) {
        int s1 = fuzzy_score(db->entries[i].name, query);
        int s2 = fuzzy_score(db->entries[i].generic_name, query);
        int s3 = fuzzy_score(db->entries[i].keywords, query);
        int s = s1;
        if (s2 > s) s = s2;
        if (s3 > s) s = s3;
        if (s >= 0) {
            tmp[ntmp].idx = i;
            tmp[ntmp].score = s;
            ntmp++;
        }
    }
    for (int i = 0; i < ntmp-1; i++) {
        for (int j = i+1; j < ntmp; j++) {
            if (tmp[j].score > tmp[i].score) {
                item_t t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t;
            }
        }
    }
    int count = ntmp < max_results ? ntmp : max_results;
    for (int i = 0; i < count; i++) {
        results[i] = tmp[i].idx;
    }
    return count;
}
