#define _GNU_SOURCE
#include "compositors/niri_ipc.h"
#include "util/log.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* Deliberately not a full JSON parser: niri's event stream is machine
 * generated and stable, so we scan for the keys we need. This mirrors the
 * field set Noctalia reads from WindowsChanged (niri_workspace_backend.cpp). */

static bool write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        off += (size_t)n;
    }
    return true;
}

/* niri announces its socket as $XDG_RUNTIME_DIR/niri.wayland-<pid>.sock; the
 * NIRI_SOCKET override wins, exactly like NiriRuntime::resolveSocketPath. */
static void niri_resolve_socket(char *out, size_t outsz)
{
    const char *env = getenv("NIRI_SOCKET");
    if (env && *env) {
        snprintf(out, outsz, "%s", env);
        return;
    }
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (!rt || !*rt) rt = "/run/user/1000";
    DIR *d = opendir(rt);
    if (!d) return;
    char best[256] = {0};
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "niri.wayland-", 14) != 0) continue;
        if (!strstr(de->d_name, ".sock")) continue;
        snprintf(best, sizeof(best), "%s/%s", rt, de->d_name);
        break;
    }
    closedir(d);
    if (best[0]) snprintf(out, outsz, "%s", best);
}

static int niri_connect(const char *path)
{
    if (!path || !*path) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path)) { close(fd); return -1; }
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    /* same request Noctalia sends to open the stream */
    static const char req[] = "\"EventStream\"\n";
    if (!write_all(fd, req, sizeof(req) - 1)) {
        close(fd);
        return -1;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

/* --- tiny scanners ------------------------------------------------------- */

static const char *json_key(const char *p, const char *end, const char *key)
{
    size_t klen = strlen(key);
    while (p < end) {
        const char *q = strstr(p, key);
        if (!q || q >= end) return NULL;
        /* require the key to be a real member: preceded by { or , */
        char before = q[-1];
        if (before == '{' || before == ',') return q + klen;
        p = q + 1;
    }
    return NULL;
}

static bool json_u64(const char *p, const char *end, const char *key, uint64_t *out)
{
    const char *k = json_key(p, end, key);
    if (!k) return false;
    while (k < end && (*k == ' ' || *k == ':')) k++;
    if (k >= end || *k < '0' || *k > '9') return false;
    *out = strtoull(k, NULL, 10);
    return true;
}

static bool json_bool(const char *p, const char *end, const char *key, bool *out)
{
    const char *k = json_key(p, end, key);
    if (!k) return false;
    while (k < end && (*k == ' ' || *k == ':')) k++;
    if (k + 4 <= end && strncmp(k, "true", 4) == 0) { *out = true; return true; }
    if (k + 5 <= end && strncmp(k, "false", 5) == 0) { *out = false; return true; }
    return false;
}

static bool json_str(const char *p, const char *end, const char *key, char *out, size_t outsz)
{
    const char *k = json_key(p, end, key);
    if (!k) return false;
    while (k < end && (*k == ' ' || *k == ':')) k++;
    if (k >= end || *k != '"') return false;
    k++;
    size_t i = 0;
    while (k < end && *k != '"' && i + 1 < outsz) {
        if (*k == '\\' && k + 1 < end) {
            k++;
            switch (*k) {
                case 'n': out[i++] = '\n'; break;
                case 't': out[i++] = '\t'; break;
                case 'r': out[i++] = '\r'; break;
                case 'u': /* keep it simple: skip the escape */ break;
                default: out[i++] = *k; break;
            }
        } else {
            out[i++] = *k;
        }
        k++;
    }
    out[i] = '\0';
    return true;
}

/* Walks the "windows":[ ... ] array and rebuilds the list. */
static bool parse_windows(struct niri_ipc *c, const char *p, const char *end)
{
    struct niri_window fresh[NIRI_MAX_WINDOWS];
    int n = 0;
    const char *arr = json_key(p, end, "\"windows\"");
    if (!arr) return false;
    arr = strchr(arr, '[');
    if (!arr || arr >= end) return false;
    arr++;
    while (arr < end && n < NIRI_MAX_WINDOWS) {
        const char *obj = strchr(arr, '{');
        if (!obj || obj >= end) break;
        /* find the matching close brace */
        int depth = 0;
        const char *q = obj;
        for (; q < end; q++) {
            if (*q == '{') depth++;
            else if (*q == '}') { depth--; if (depth == 0) break; }
        }
        if (q >= end) break;
        struct niri_window w;
        memset(&w, 0, sizeof(w));
        json_u64(obj, q, "\"id\"", &w.id);
        json_str(obj, q, "\"title\"", w.title, sizeof(w.title));
        json_str(obj, q, "\"app_id\"", w.app_id, sizeof(w.app_id));
        uint64_t ws = 0;
        if (json_u64(obj, q, "\"workspace_id\"", &ws)) w.workspace_id = (int)ws;
        json_bool(obj, q, "\"is_focused\"", &w.focused);
        json_bool(obj, q, "\"is_urgent\"", &w.urgent);
        if (w.id != 0) fresh[n++] = w;
        arr = q + 1;
    }

    bool changed = (n != c->n_windows);
    if (!changed) {
        for (int i = 0; i < n; i++) {
            if (memcmp(&fresh[i], &c->windows[i], sizeof(fresh[i])) != 0) { changed = true; break; }
        }
    }
    memcpy(c->windows, fresh, sizeof(fresh));
    c->n_windows = n;
    return changed;
}

struct niri_ipc *niri_ipc_create(void (*on_change)(void *userdata), void *userdata)
{
    struct niri_ipc *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->fd = -1;
    c->on_change = on_change;
    c->userdata = userdata;
    niri_ipc_reconnect(c);
    return c;
}

void niri_ipc_reconnect(struct niri_ipc *c)
{
    if (!c || c->fd >= 0) return;
    niri_resolve_socket(c->socket_path, sizeof(c->socket_path));
    c->fd = niri_connect(c->socket_path);
    c->rlen = 0;
    if (c->fd >= 0) log_info("niri ipc connected: %s", c->socket_path);
    else log_warn("niri ipc unavailable (no socket)");
}

void niri_ipc_destroy(struct niri_ipc *c)
{
    if (!c) return;
    if (c->fd >= 0) close(c->fd);
    free(c);
}

int niri_ipc_fd(struct niri_ipc *c)
{
    return c ? c->fd : -1;
}

bool niri_ipc_dispatch(struct niri_ipc *c)
{
    if (!c || c->fd < 0) return false;
    char buf[8192];
    bool changed = false;
    ssize_t n;
    while ((n = read(c->fd, buf, sizeof(buf))) > 0) {
        if (c->rlen + (size_t)n >= sizeof(c->rbuf)) c->rlen = 0; /* resync */
        memcpy(c->rbuf + c->rlen, buf, (size_t)n);
        c->rlen += (size_t)n;

        /* process complete lines */
        size_t start = 0;
        for (size_t i = 0; i < c->rlen; i++) {
            if (c->rbuf[i] != '\n') continue;
            c->rbuf[i] = '\0';
            const char *line = c->rbuf + start;
            if (strstr(line, "WindowsChanged")) {
                if (parse_windows(c, line, line + strlen(line))) changed = true;
            }
            start = i + 1;
        }
        if (start > 0) {
            memmove(c->rbuf, c->rbuf + start, c->rlen - start);
            c->rlen -= start;
        }
        if (c->rlen + (size_t)n >= sizeof(c->rbuf)) c->rlen = 0;
    }
    if (n == 0) { /* peer closed: reconnect on the next tick */
        close(c->fd);
        c->fd = -1;
        c->rlen = 0;
        log_warn("niri ipc stream closed");
    }
    if (changed && c->on_change) c->on_change(c->userdata);
    return changed;
}

int niri_ipc_window_count(const struct niri_ipc *c)
{
    return c ? c->n_windows : 0;
}

const struct niri_window *niri_ipc_window(const struct niri_ipc *c, int idx)
{
    if (!c || idx < 0 || idx >= c->n_windows) return NULL;
    return &c->windows[idx];
}

static bool niri_request(const char *json)
{
    const char *env = getenv("NIRI_SOCKET");
    char path[256];
    if (env && *env) snprintf(path, sizeof(path), "%s", env);
    else niri_resolve_socket(path, sizeof(path));
    if (!path[0]) return false;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return false; }
    size_t len = strlen(json);
    bool ok = write_all(fd, json, len);
    if (ok) {
        char r[256];
        ssize_t rn = read(fd, r, sizeof(r) - 1);
        if (rn > 0) { r[rn] = '\0'; ok = strstr(r, "Ok") != NULL; }
    }
    close(fd);
    return ok;
}

bool niri_ipc_focus(struct niri_ipc *c, uint64_t id)
{
    (void)c;
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"Action\":{\"FocusWindow\":{\"id\":%llu}}}\n",
             (unsigned long long)id);
    bool ok = niri_request(buf);
    log_info("niri FocusWindow %llu -> %s", (unsigned long long)id, ok ? "ok" : "failed");
    return ok;
}

bool niri_ipc_close(struct niri_ipc *c, uint64_t id)
{
    (void)c;
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"Action\":{\"CloseWindow\":{\"id\":%llu}}}\n",
             (unsigned long long)id);
    bool ok = niri_request(buf);
    log_info("niri CloseWindow %llu -> %s", (unsigned long long)id, ok ? "ok" : "failed");
    return ok;
}