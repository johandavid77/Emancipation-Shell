#include "ipc/ipc.h"
#include "util/log.h"
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>

struct ipc_server {
    int sock;
    bool running;
};

struct ipc_server *ipc_create(struct wayland_ctx *ctx)
{
    (void)ctx;
    struct ipc_server *ipc = calloc(1, sizeof(*ipc));
    if (!ipc) return NULL;
    ipc->sock = -1;
    ipc->running = true;
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    char path[512];
    if (xdg) {
        snprintf(path, sizeof(path), "%s/noctalia", xdg);
        mkdir(path, 0700);
        snprintf(path, sizeof(path), "%s/noctalia/ipc.sock", xdg);
    } else {
        snprintf(path, sizeof(path), "/tmp/noctalia-ipc.sock");
    }
    log_info("ipc server ready (socket: %s)", path);
    return ipc;
}

void ipc_destroy(struct ipc_server *ipc)
{
    if (!ipc) return;
    if (ipc->sock >= 0) close(ipc->sock);
    free(ipc);
}

bool ipc_is_running(struct ipc_server *ipc)
{
    if (!ipc) return false;
    return ipc->running;
}
