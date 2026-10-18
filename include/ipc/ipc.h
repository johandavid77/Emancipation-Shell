#ifndef EMANCIPATION_IPC_H
#define EMANCIPATION_IPC_H

#include <stdbool.h>

struct ipc_server;
struct wayland_ctx;

/* Handles a single command line (without the trailing newline). It may return
 * a malloc'd string that is sent back to the client, or NULL for an empty
 * reply. The string is freed by the caller. */
typedef char *(*ipc_command_fn)(const char *cmd, void *userdata);

struct ipc_server *ipc_create(struct wayland_ctx *ctx, ipc_command_fn fn, void *userdata);
void ipc_destroy(struct ipc_server *ipc);
bool ipc_is_running(struct ipc_server *ipc);

/* Listen socket fd for the main poll loop (POLLIN when a client connects). */
int ipc_fd(struct ipc_server *ipc);

/* Accept/read pending clients and run their commands. Returns true if at
 * least one command was processed (so the UI can be marked dirty). */
bool ipc_dispatch(struct ipc_server *ipc);

/* Client side: connect to a running shell, send one command, return the reply
 * (malloc'd, caller frees) or NULL on failure. */
char *ipc_send_command(const char *cmd);

#endif /* EMANCIPATION_IPC_H */
