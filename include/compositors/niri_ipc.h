#ifndef EMANCIPATION_NIRI_IPC_H
#define EMANCIPATION_NIRI_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* niri IPC client, mirroring Noctalia's NiriRuntime (compositors/niri/):
 * a newline-delimited JSON protocol over the compositor's unix socket. The
 * event stream gives us window titles, app ids, focus and urgency, which is
 * what a taskbar needs; activate/close go back as Action requests. */

#define NIRI_MAX_WINDOWS 64

struct niri_window {
    uint64_t id;
    char title[256];
    char app_id[128];
    int workspace_id;
    bool focused;
    bool urgent;
};

struct niri_ipc {
    int fd;             /* event stream socket, -1 when disconnected */
    char socket_path[256];
    char rbuf[262144];
    size_t rlen;
    struct niri_window windows[NIRI_MAX_WINDOWS];
    int n_windows;
    void (*on_change)(void *userdata);
    void *userdata;
};

struct niri_ipc *niri_ipc_create(void (*on_change)(void *userdata), void *userdata);
void niri_ipc_destroy(struct niri_ipc *c);
int niri_ipc_fd(struct niri_ipc *c);
/* Call after POLLIN. Returns true when the window list changed. */
bool niri_ipc_dispatch(struct niri_ipc *c);
/* Re-resolve and reconnect; safe to call when fd was -1. */
void niri_ipc_reconnect(struct niri_ipc *c);
int niri_ipc_window_count(const struct niri_ipc *c);
const struct niri_window *niri_ipc_window(const struct niri_ipc *c, int idx);
bool niri_ipc_focus(struct niri_ipc *c, uint64_t id);
bool niri_ipc_close(struct niri_ipc *c, uint64_t id);

#endif /* EMANCIPATION_NIRI_IPC_H */