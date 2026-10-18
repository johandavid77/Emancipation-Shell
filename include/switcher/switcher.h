#ifndef EMANCIPATION_SWITCHER_H
#define EMANCIPATION_SWITCHER_H

#include <stdbool.h>
#include "core/panel.h"

struct wayland_ctx;
struct output;
struct config;
struct niri_ipc;

/* Window switcher overlay: a centered, keyboard-driven panel listing the
 * compositor's windows (niri IPC). Tab/arrows cycle, Enter focuses the
 * selection, Escape dismisses. Mirrors Noctalia's alt-tab switcher panel. */

struct switcher;

struct switcher *switcher_create(struct wayland_ctx *ctx, const struct config **cfg,
                                 struct output *out);
void switcher_destroy(struct switcher *sw);
void switcher_set_niri(struct switcher *sw, struct niri_ipc *niri);
void switcher_set_config(struct switcher *sw, const struct config *cfg);
struct panel *switcher_surface(struct switcher *sw);
void switcher_show(struct switcher *sw, struct output *out);
void switcher_hide(struct switcher *sw);
void switcher_toggle(struct switcher *sw, struct output *out);
bool switcher_visible(struct switcher *sw);
void switcher_next(struct switcher *sw);
void switcher_prev(struct switcher *sw);
/* Focus the selected window and dismiss the overlay. */
void switcher_select(struct switcher *sw);
/* Called when the compositor's window list changed. */
void switcher_refresh(struct switcher *sw);

#endif /* EMANCIPATION_SWITCHER_H */
