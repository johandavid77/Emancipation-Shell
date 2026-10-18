#ifndef EMANCIPATION_CONTROL_CENTER_H
#define EMANCIPATION_CONTROL_CENTER_H

#include <stdbool.h>

struct control_center;
struct wayland_ctx;
struct output;
struct config;
struct panel;
struct session_actions;
struct lock_screen;
struct notification_store;
struct niri_ipc;

struct control_center *control_center_create(struct wayland_ctx *ctx, const struct config **cfg,
                                             struct output *out);
void control_center_destroy(struct control_center *cc);
struct panel *control_center_surface(struct control_center *cc);
void control_center_set_actions(struct control_center *cc, struct session_actions *sa,
                                struct lock_screen *lock);
/* Notification history shown by the "Notificaciones" tab. */
void control_center_set_notifications(struct control_center *cc, struct notification_store *store);
/* Compositor window list shown by the "Pantalla" tab. */
void control_center_set_niri(struct control_center *cc, struct niri_ipc *niri);
/* Optional handler for shortcuts that act on the shell itself (launcher, screen
 * off). `id` is the shortcut id without the leading '@'. */
void control_center_set_shortcut_handler(struct control_center *cc,
                                         void (*fn)(void *userdata, const char *id),
                                         void *userdata);
/* Select a tab (0..3) - used by IPC/tests. */
void control_center_set_tab(struct control_center *cc, int tab);
void control_center_toggle_proxy(void *userdata);
void control_center_tick(struct control_center *cc);

#endif /* EMANCIPATION_CONTROL_CENTER_H */
