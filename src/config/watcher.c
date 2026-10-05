#include "config/watcher.h"
#include "config/parser.h"
#include "config/config.h"
#include "util/log.h"
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <stdio.h>

#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

struct config_watcher {
    int inotify_fd;
    int wd;
    void *src;
    char path[512];
    char dir[512];
    struct config last;
    char last_err[512];
    bool has_err;
    void *userdata;
    config_changed_cb cb;
};

static void apply_config(struct config_watcher *w, const struct config *newcfg, bool ok, const char *err)
{
    if (!w) return;
    if (ok && newcfg) {
        if (!config_equal(&w->last, newcfg)) {
            memcpy(&w->last, newcfg, sizeof(w->last));
            w->has_err = false;
            w->last_err[0] = '\0';
            if (w->cb) w->cb(w->userdata, &w->last, true);
            log_info("config reloaded successfully");
        } else {
            if (w->cb) w->cb(w->userdata, &w->last, true);
            log_debug("config unchanged");
        }
    } else {
        w->has_err = true;
        strncpy(w->last_err, err ? err : "unknown error", sizeof(w->last_err)-1);
        w->last_err[sizeof(w->last_err)-1] = '\0';
        if (w->cb) w->cb(w->userdata, &w->last, false);
        log_warn("config reload failed: %s (keeping last valid)", w->last_err);
    }
}

void config_watcher_reload_now(struct config_watcher *w)
{
    if (!w) return;
    struct config nc;
    char err[512];
    if (config_load_from_file(w->path, &nc, err, sizeof(err))) {
        apply_config(w, &nc, true, NULL);
    } else {
        apply_config(w, NULL, false, err);
    }
}

static void ensure_dir_exists(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) == 0) return;
    mkdir(dir, 0755);
}

struct config_watcher *config_watcher_create(const char *path, void *loop, void *userdata, config_changed_cb cb)
{
    (void)loop;
    if (!path) return NULL;
    struct config_watcher *w = calloc(1, sizeof(*w));
    if (!w) return NULL;
    strncpy(w->path, path, sizeof(w->path)-1);
    w->path[sizeof(w->path)-1] = '\0';
    char tmpdir[512];
    strncpy(tmpdir, path, sizeof(tmpdir)-1);
    tmpdir[sizeof(tmpdir)-1] = '\0';
    char *d = dirname(tmpdir);
    if (d) {
        strncpy(w->dir, d, sizeof(w->dir)-1);
        w->dir[sizeof(w->dir)-1] = '\0';
    } else {
        snprintf(w->dir, sizeof(w->dir), ".");
    }
    ensure_dir_exists(w->dir);
    config_defaults(&w->last);
    w->userdata = userdata;
    w->cb = cb;
    w->has_err = false;
    w->last_err[0] = '\0';
    w->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    w->wd = -1;
    w->src = NULL;
    if (w->inotify_fd >= 0) {
        /* watch the directory: editors usually replace the file atomically */
        w->wd = inotify_add_watch(w->inotify_fd, w->dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
        if (w->wd < 0) log_warn("inotify_add_watch(%s) failed: %s", w->dir, strerror(errno));
    } else {
        log_warn("inotify_init1 failed: %s", strerror(errno));
    }
    return w;
}

void config_watcher_destroy(struct config_watcher *w)
{
    if (!w) return;
    if (w->inotify_fd >= 0) {
        close(w->inotify_fd);
        w->inotify_fd = -1;
    }
    free(w);
}

const struct config *config_watcher_get_last(struct config_watcher *w)
{
    if (!w) return NULL;
    return &w->last;
}

bool config_watcher_has_error(struct config_watcher *w)
{
    if (!w) return false;
    return w->has_err;
}

const char *config_watcher_last_error(struct config_watcher *w)
{
    if (!w) return "";
    return w->last_err;
}

int config_watcher_get_fd(struct config_watcher *w)
{
    return w ? w->inotify_fd : -1;
}

void config_watcher_dispatch(struct config_watcher *w)
{
    if (!w || w->inotify_fd < 0) return;
    char buf[BUF_LEN] __attribute__((aligned(__alignof__(struct inotify_event))));
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", w->path);
    const char *base = basename(tmp);
    bool reload = false;
    for (;;) {
        ssize_t len = read(w->inotify_fd, buf, sizeof(buf));
        if (len <= 0) break;
        for (char *p = buf; p < buf + len;) {
            const struct inotify_event *ev = (const struct inotify_event *)p;
            if (ev->len && strcmp(ev->name, base) == 0) reload = true;
            p += sizeof(struct inotify_event) + ev->len;
        }
    }
    if (reload) config_watcher_reload_now(w);
}
