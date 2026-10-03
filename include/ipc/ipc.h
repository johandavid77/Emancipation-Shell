#ifndef EMANCIPATION_IPC_H
#define EMANCIPATION_IPC_H

#include <stdbool.h>

struct ipc_server;
struct wayland_ctx;

struct ipc_server *ipc_create(struct wayland_ctx *ctx);
void ipc_destroy(struct ipc_server *ipc);
bool ipc_is_running(struct ipc_server *ipc);

#endif /* EMANCIPATION_IPC_H */
