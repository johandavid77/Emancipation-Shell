#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include "wayland-client.h"
#include "core/wayland.h"
#include "core/output.h"
#include "core/registry.h"
#include "core/surface_mgr.h"
#include "util/log.h"
#include "config/config.h"
#include "config/parser.h"
#include "config/watcher.h"
#include "bar/workspaces.h"
#include "bar/bar.h"
#include "launcher/launcher.h"
#include "dock/dock.h"
#include "control/control_center.h"
#include "osd/osd.h"
#include "notify/notify_daemon.h"
#include "session/lock_screen.h"
#include "session/session_actions.h"
#include "wallpaper/wallpaper.h"
#include "hotkeys/hotkeys.h"
#include "ipc/ipc.h"
#include "clipboard/clipboard.h"
#include "zwlr-layer-shell-v1-client-protocol.h"
#include "ext-workspace-v1-client-protocol.h"
#include "ext-session-lock-v1-client-protocol.h"

static int g_shutdown = 0;

static void handle_signal(int sig)
{
    (void)sig;
    g_shutdown = 1;
}

static void on_config_changed(void *userdata, const struct config *cfg, bool applied)
{
    (void)userdata;
    if (applied) {
        log_info("config applied: bar.height=%d font.size=%d", cfg->bar.height, cfg->font.size);
    } else {
        log_warn("config not applied, keeping last valid");
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    log_set_level(LOG_INFO);

    const char *display_name = getenv("WAYLAND_DISPLAY");
    if (!display_name || display_name[0] == '\0') {
        display_name = "wayland-0";
    }

    struct wl_display *display = wl_display_connect(display_name);
    if (!display) {
        log_err("failed to connect to Wayland display: %s", display_name);
        return 1;
    }
    log_info("connected to Wayland display: %s", display_name);

    struct wayland_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.display = display;

    struct wl_list outputs;
    wl_list_init(&outputs);

    struct surface_mgr mgr;
    surface_mgr_init(&mgr, &ctx, &outputs);

    struct registry_state reg_state;
    registry_init(&reg_state, &ctx, &outputs, &mgr);

    registry_bind_globals(&reg_state);
    wl_display_roundtrip(display);

    if (!ctx.compositor) {
        log_err("wl_compositor not available");
        surface_mgr_fini(&mgr);
        if (ctx.registry) wl_registry_destroy(ctx.registry);
        wl_display_disconnect(display);
        return 1;
    }

    const char *cfg_path = config_default_path();
    struct config_watcher *cw = config_watcher_create(cfg_path, NULL, NULL, on_config_changed);
    if (cw) {
        config_watcher_reload_now(cw);
        log_info("config loaded: %s", cfg_path);
    } else {
        log_warn("config watcher could not be initialized");
    }
    const struct config *cfg_last = NULL;
    struct config cfg_def;
    if (cw) {
        cfg_last = config_watcher_get_last(cw);
    }
    if (!cfg_last) {
        config_defaults(&cfg_def);
        cfg_last = &cfg_def;
    }

    struct workspace_manager *wm = workspaces_create(&ctx);
    if (wm && ctx.workspace_manager) {
        workspaces_bind(wm);
    }

    struct launcher *launcher = launcher_create(&ctx);
    struct dock *dock = NULL;
    struct control_center *cc = NULL;
    struct osd *osd = NULL;
    struct notify_daemon *nd = NULL;
    struct lock_screen *ls = NULL;
    struct session_actions *sa = NULL;
    struct wallpaper *wp = NULL;
    struct hotkeys *hk = NULL;
    struct ipc_server *ipc = NULL;
    struct clipboard *clip = NULL;
    struct output *out_first = NULL;
    if (!wl_list_empty(&outputs)) {
        struct wl_list *first = outputs.next;
        out_first = (struct output *)((char *)first - offsetof(struct output, link));
    }
    if (out_first) {
        dock = dock_create(&ctx, out_first, cfg_last);
        cc = control_center_create(&ctx, out_first);
        osd = osd_create(&ctx, out_first);
        wp = wallpaper_create(&ctx, out_first);
    }
    nd = notify_daemon_create(&ctx);
    ls = lock_screen_create(&ctx);
    sa = session_actions_create();
    hk = hotkeys_create();
    ipc = ipc_create(&ctx);
    clip = clipboard_create(100);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    log_info("surface manager ready");
    while (!g_shutdown && !mgr.shutdown) {
        int ret = wl_display_dispatch(display);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            /* Ignore protocol errors from compositor that we didn't trigger */
            continue;
        }
    }

    log_info("shutting down...");
    if (clip) clipboard_destroy(clip);
    if (ipc) ipc_destroy(ipc);
    if (hk) hotkeys_destroy(hk);
    if (sa) session_actions_destroy(sa);
    if (ls) lock_screen_destroy(ls);
    if (nd) notify_daemon_destroy(nd);
    if (osd) osd_destroy(osd);
    if (cc) control_center_destroy(cc);
    if (dock) dock_destroy(dock);
    if (launcher) launcher_destroy(launcher);
    if (wm) workspaces_destroy(wm);
    if (cw) config_watcher_destroy(cw);
    if (wp) wallpaper_destroy(wp);
    surface_mgr_fini(&mgr);
    struct output *out, *tmp;
    wl_list_for_each_safe(out, tmp, &outputs, link) {
        wl_list_remove(&out->link);
        output_destroy(out);
        free(out);
    }
    if (ctx.workspace_manager) ext_workspace_manager_v1_destroy(ctx.workspace_manager);
    if (ctx.layer_shell) zwlr_layer_shell_v1_destroy(ctx.layer_shell);
    if (ctx.shm) wl_shm_destroy(ctx.shm);
    if (ctx.compositor) wl_compositor_destroy(ctx.compositor);
    if (ctx.registry) wl_registry_destroy(ctx.registry);
    wl_display_disconnect(display);
    log_info("shutdown complete");
    return 0;
}
