#define _POSIX_C_SOURCE 200809L
#include "ipc/ipc.h"
#include "util/log.h"
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

#define IPC_MAX_CLIENTS 16
#define IPC_LINE_MAX 2048

struct ipc_client {
    int fd;
    char buf[IPC_LINE_MAX];
    size_t len;
};

struct ipc_server {
    int sock;
    bool running;
    ipc_command_fn handler;
    void *userdata;
    struct ipc_client clients[IPC_MAX_CLIENTS];
};

static void ipc_socket_path(char *buf, size_t sz)
{
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg && xdg[0]) {
        snprintf(buf, sz, "%s/emancipation", xdg);
        mkdir(buf, 0700);
        snprintf(buf, sz, "%s/emancipation/ipc.sock", xdg);
    } else {
        snprintf(buf, sz, "/tmp/emancipation-%u.sock", (unsigned)getuid());
    }
}

static int set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

struct ipc_server *ipc_create(struct wayland_ctx *ctx, ipc_command_fn fn, void *userdata)
{
    (void)ctx;
    struct ipc_server *ipc = calloc(1, sizeof(*ipc));
    if (!ipc) return NULL;
    ipc->sock = -1;
    ipc->handler = fn;
    ipc->userdata = userdata;
    for (int i = 0; i < IPC_MAX_CLIENTS; i++) ipc->clients[i].fd = -1;

    char path[512];
    ipc_socket_path(path, sizeof(path));

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        log_err("ipc: socket(): %s", strerror(errno));
        free(ipc);
        return NULL;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_err("ipc: bind(%s): %s", path, strerror(errno));
        close(fd);
        free(ipc);
        return NULL;
    }
    if (listen(fd, 8) < 0) {
        log_err("ipc: listen(): %s", strerror(errno));
        close(fd);
        unlink(path);
        free(ipc);
        return NULL;
    }
    set_nonblock(fd);
    ipc->sock = fd;
    ipc->running = true;
    log_info("ipc server ready (socket: %s)", path);
    return ipc;
}

void ipc_destroy(struct ipc_server *ipc)
{
    if (!ipc) return;
    for (int i = 0; i < IPC_MAX_CLIENTS; i++) {
        if (ipc->clients[i].fd >= 0) close(ipc->clients[i].fd);
    }
    if (ipc->sock >= 0) {
        close(ipc->sock);
        char path[512];
        ipc_socket_path(path, sizeof(path));
        unlink(path);
    }
    free(ipc);
}

bool ipc_is_running(struct ipc_server *ipc)
{
    return ipc && ipc->running;
}

int ipc_fd(struct ipc_server *ipc)
{
    return ipc ? ipc->sock : -1;
}

static void ipc_reply(int fd, const char *msg)
{
    const char *r = msg ? msg : "";
    size_t len = strlen(r);
    ssize_t n = write(fd, r, len);
    if (n < 0) { /* best effort */ }
    write(fd, "\n", 1);
}

static void ipc_close_client(struct ipc_server *ipc, struct ipc_client *c)
{
    (void)ipc;
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
    c->len = 0;
}

static void ipc_run_line(struct ipc_server *ipc, struct ipc_client *c, char *line)
{
    while (*line == ' ' || *line == '\t') line++;
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t'))
        line[--n] = '\0';

    char *reply = NULL;
    if (ipc->handler) reply = ipc->handler(line, ipc->userdata);
    ipc_reply(c->fd, reply ? reply : "ok");
    free(reply);
    ipc_close_client(ipc, c);
}

bool ipc_dispatch(struct ipc_server *ipc)
{
    if (!ipc || ipc->sock < 0) return false;
    bool did = false;

    for (;;) {
        int cfd = accept(ipc->sock, NULL, NULL);
        if (cfd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                log_warn("ipc: accept: %s", strerror(errno));
            break;
        }
        set_nonblock(cfd);
        int slot = -1;
        for (int i = 0; i < IPC_MAX_CLIENTS; i++) {
            if (ipc->clients[i].fd < 0) { slot = i; break; }
        }
        if (slot < 0) { close(cfd); continue; }
        ipc->clients[slot].fd = cfd;
        ipc->clients[slot].len = 0;
    }

    for (int i = 0; i < IPC_MAX_CLIENTS; i++) {
        struct ipc_client *c = &ipc->clients[i];
        if (c->fd < 0) continue;
        ssize_t n = read(c->fd, c->buf + c->len, sizeof(c->buf) - 1 - c->len);
        if (n == 0) { ipc_close_client(ipc, c); continue; }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            ipc_close_client(ipc, c);
            continue;
        }
        c->len += (size_t)n;
        c->buf[c->len] = '\0';
        char *nl = memchr(c->buf, '\n', c->len);
        if (!nl) {
            if (c->len >= sizeof(c->buf) - 1) {
                /* oversized command: run what we have */
                ipc_run_line(ipc, c, c->buf);
                did = true;
            }
            continue;
        }
        *nl = '\0';
        ipc_run_line(ipc, c, c->buf);
        did = true;
    }
    return did;
}

char *ipc_send_command(const char *cmd)
{
    char path[512];
    ipc_socket_path(path, sizeof(path));

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return NULL;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NULL;
    }
    char line[IPC_LINE_MAX];
    snprintf(line, sizeof(line), "%s\n", cmd ? cmd : "");
    if (write(fd, line, strlen(line)) < 0) {
        close(fd);
        return NULL;
    }
    char reply[IPC_LINE_MAX];
    ssize_t n = read(fd, reply, sizeof(reply) - 1);
    close(fd);
    if (n < 0) return NULL;
    reply[n] = '\0';
    char *nl = strchr(reply, '\n');
    if (nl) *nl = '\0';
    return strdup(reply);
}
