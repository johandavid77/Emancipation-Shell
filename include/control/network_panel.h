#ifndef EMANCIPATION_NETWORK_PANEL_H
#define EMANCIPATION_NETWORK_PANEL_H

#include <stdbool.h>
#include "core/panel.h"

struct network_panel;
struct wayland_ctx;
struct output;
struct config;
struct backends_state;
struct panel;

/* Popup with the details of the active link (IP, gateway, DNS, MAC, signal)
 * opened by clicking the network widget on the bar. */
struct network_panel *network_panel_create(struct wayland_ctx *ctx, const struct config **cfg,
                                           struct output *out);
void network_panel_destroy(struct network_panel *np);
struct panel *network_panel_surface(struct network_panel *np);
/* Interface/signal snapshot owned by the bar backends; may be NULL. */
void network_panel_set_backends(struct network_panel *np, const struct backends_state *bk);
void network_panel_toggle_proxy(void *userdata);
/* Polls the in-flight connect/disconnect and refreshes the AP list; called
 * from the shell's 1s tick while a Wi-Fi operation is running. */
void network_panel_tick(struct network_panel *np);

/* Scripting/IPC surface: open the Wi-Fi tab, rescan, drive the selection and
 * report state (used by `emancipation-shell network ...`). */
void network_panel_show_wifi(struct network_panel *np);
void network_panel_show_details(struct network_panel *np);
void network_panel_scan(struct network_panel *np);
int network_panel_connect_index(struct network_panel *np, int idx, const char *password);
int network_panel_disconnect_current(struct network_panel *np);
void network_panel_status(struct network_panel *np, char *out, size_t n);


#endif /* EMANCIPATION_NETWORK_PANEL_H */
