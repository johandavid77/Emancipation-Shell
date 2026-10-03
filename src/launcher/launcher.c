#include "launcher/launcher.h"
#include "core/wayland.h"
#include "launcher/desktop.h"
#include "util/log.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

struct launcher {
    struct wayland_ctx *ctx;
    bool visible;
    char query[256];
    struct desktop_db db;
    int last_results[32];
    int last_count;
};

static void launcher_spawn(const char *cmd)
{
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
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
    desktop_db_free(&l->db);
    free(l);
}

void launcher_show(struct launcher *l)
{
    if (!l) return;
    l->visible = true;
    l->query[0] = '\0';
    l->last_count = 0;
    log_info("launcher shown");
}

void launcher_hide(struct launcher *l)
{
    if (!l) return;
    l->visible = false;
    log_info("launcher hidden");
}

void launcher_toggle(struct launcher *l)
{
    if (!l) return;
    if (l->visible) launcher_hide(l);
    else launcher_show(l);
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
