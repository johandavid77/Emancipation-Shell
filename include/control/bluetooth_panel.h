#ifndef EMANCIPATION_BLUETOOTH_PANEL_H
#define EMANCIPATION_BLUETOOTH_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include "core/panel.h"

struct bluetooth_panel;
struct wayland_ctx;
struct output;
struct config;
struct panel;

/* Popup with the full Bluetooth management (power, scan, pair, connect,
 * disconnect, forget) driven by bluetoothctl. */
struct bluetooth_panel *bluetooth_panel_create(struct wayland_ctx *ctx, const struct config **cfg,
                                               struct output *out);
void bluetooth_panel_destroy(struct bluetooth_panel *bp);
struct panel *bluetooth_panel_surface(struct bluetooth_panel *bp);
void bluetooth_panel_toggle_proxy(void *userdata);

/* 1s tick: polls in-flight operations and refreshes while scanning. */
void bluetooth_panel_tick(struct bluetooth_panel *bp);

/* Scripting/IPC surface (`emancipation-shell bluetooth ...`). */
void bluetooth_panel_open(struct bluetooth_panel *bp);
void bluetooth_panel_scan(struct bluetooth_panel *bp);
int bluetooth_panel_select(struct bluetooth_panel *bp, int idx);
int bluetooth_panel_primary_action(struct bluetooth_panel *bp, int idx);
int bluetooth_panel_disconnect(struct bluetooth_panel *bp, int idx);
int bluetooth_panel_pair(struct bluetooth_panel *bp, int idx);
int bluetooth_panel_forget(struct bluetooth_panel *bp, int idx);
void bluetooth_panel_status(struct bluetooth_panel *bp, char *out, size_t n);

#endif /* EMANCIPATION_BLUETOOTH_PANEL_H */
