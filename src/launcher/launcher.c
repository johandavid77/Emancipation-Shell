#include "launcher/launcher.h"
#include "core/wayland.h"
#include "launcher/desktop.h"
#include "launcher/icons.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define ICON_CACHE_SLOTS 48

struct launcher {
    struct wayland_ctx *ctx;
    bool visible;
    char query[256];
    struct desktop_db db;
    int last_results[32];
    int last_count;
    /* icon cache: entry index -> decoded surface (lazily filled) */
    cairo_surface_t *icons[ICON_CACHE_SLOTS];
    int icon_idx[ICON_CACHE_SLOTS];
    int icon_n;
};

cairo_surface_t *launcher_icon_for(struct launcher *l, int idx)
{
    if (!l || idx < 0 || idx >= l->db.count) return NULL;
    for (int i = 0; i < l->icon_n; i++) {
        if (l->icon_idx[i] == idx) return l->icons[i];
    }
    if (l->icon_n >= ICON_CACHE_SLOTS) return NULL; /* cache full: draw text only */
    const char *name = l->db.entries[idx].icon;
    cairo_surface_t *s = name[0] ? icon_load_surface(name, 48) : NULL;
    l->icon_idx[l->icon_n] = idx;
    l->icons[l->icon_n] = s;
    l->icon_n++;
    if (s) log_debug("launcher icon loaded: %s", name);
    return s;
}

static void launcher_spawn(const char *cmd)
{
    if (!cmd) return;
    char buf[512];
    strncpy(buf, cmd, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    /* strip %U %u %F %f %i %c %k */
    char *p = buf;
    while ((p = strchr(p, '%'))) {
        if (p[1] && strchr("UuFficKk", p[1])) {
            memmove(p, p+2, strlen(p+2)+1);
        } else {
            p++;
        }
    }
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", buf, (char *)NULL);
        _exit(1);
    } else if (pid > 0) {
        waitpid(pid, NULL, WNOHANG);
    }
}

struct launcher *launcher_create(struct wayland_ctx *ctx)
{
    if (!ctx) return NULL;
    struct launcher *l = calloc(1, sizeof(*l));
    if (!l) return NULL;
    l->ctx = ctx;
    l->visible = false;
    l->query[0] = '\0';
    desktop_db_init(&l->db);
    desktop_db_load_system(&l->db);
    log_info("launcher loaded %d desktop entries", l->db.count);
    return l;
}

void launcher_destroy(struct launcher *l)
{
    if (!l) return;
    for (int i = 0; i < l->icon_n; i++) {
        if (l->icons[i]) cairo_surface_destroy(l->icons[i]);
    }
    desktop_db_free(&l->db);
    free(l);
}

void launcher_show(struct launcher *l)
{
    if (!l) return;
    l->visible = true;
    l->query[0] = '\0';
    l->last_count = 0;
    log_info("launcher shown (visible=%d)", l->visible);
}

void launcher_hide(struct launcher *l)
{
    if (!l) return;
    l->visible = false;
    log_info("launcher hidden (visible=%d)", l->visible);
}

void launcher_toggle(struct launcher *l)
{
    if (!l) return;
    if (l->visible) launcher_hide(l);
    else { launcher_show(l); launcher_spawn("true"); }
}

bool launcher_is_visible(struct launcher *l)
{
    if (!l) return false;
    return l->visible;
}

void launcher_set_query(struct launcher *l, const char *q)
{
    if (!l || !q) return;
    strncpy(l->query, q, sizeof(l->query)-1);
    l->query[sizeof(l->query)-1] = '\0';
    l->last_count = desktop_db_search_fuzzy(&l->db, l->query, l->last_results, 32);
}

void launcher_exec_selected(struct launcher *l)
{
    if (!l) return;
    if (l->last_count > 0 && l->last_results[0] >= 0 && l->last_results[0] < l->db.count) {
        const struct desktop_entry *e = &l->db.entries[l->last_results[0]];
        log_info("launcher exec: %s", e->name);
        launcher_spawn(e->exec);
    }
    launcher_hide(l);
}

void launcher_get_query(struct launcher *l, char *out, size_t outsz)
{
    if (!l || !out || outsz == 0) return;
    strncpy(out, l->query, outsz - 1);
    out[outsz - 1] = '\0';
}

void launcher_get_results(struct launcher *l, int *results, int *count_out, int maxn)
{
    if (!l || !results || !count_out || maxn <= 0) return;
    int c = desktop_db_search_fuzzy(&l->db, l->query, results, maxn);
    *count_out = c;
}

const char *launcher_get_name(struct launcher *l, int idx)
{
    if (!l || idx < 0 || idx >= l->db.count) return NULL;
    return l->db.entries[idx].name;
}

void launcher_exec_from_idx(struct launcher *l, int idx)
{
    if (!l) return;
    if (idx >= 0 && idx < l->db.count) {
        const char *cmd = l->db.entries[idx].exec;
        launcher_spawn(cmd);
        l->visible = false;
    }
}
